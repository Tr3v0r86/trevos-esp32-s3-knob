/* sim/stubs/sim_nvs.c — in-memory NVS.
 *
 * Scalar-only (u32/u8/i32), which is all main.c's pomo_persist_* uses. Two freeze-repro
 * knobs on nvs_commit:
 *   SIM_NVS_COMMIT_DELAY_MS  — sleep inside commit WHILE the caller holds the lvgl_port
 *                              lock, to widen the lock-held window (models a flash stall).
 *   begin/end logging        — proves whether a hang is inside the commit or elsewhere.
 */
#include "nvs.h"
#include "nvs_flash.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define MAX_NS   8
#define MAX_ENT  128

static char     s_ns[MAX_NS][16];
static int      s_ns_count = 0;

typedef struct { char k[80]; uint32_t v; int used; uint8_t blob[4096]; size_t blob_len; } ent_t;   /* v holds u32/u8/i32 bits */
static ent_t    s_ent[MAX_ENT];

esp_err_t nvs_flash_init(void)  { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { memset(s_ent, 0, sizeof(s_ent)); return ESP_OK; }

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out)
{
    (void)mode;
    for (int i = 0; i < s_ns_count; i++)
        if (strcmp(s_ns[i], name) == 0) { *out = (nvs_handle_t)(i + 1); return ESP_OK; }
    if (s_ns_count >= MAX_NS) return ESP_FAIL;
    strncpy(s_ns[s_ns_count], name, sizeof(s_ns[0]) - 1);
    *out = (nvs_handle_t)(++s_ns_count);
    return ESP_OK;
}

void nvs_close(nvs_handle_t h) { (void)h; }

static ent_t *find_or_add(nvs_handle_t h, const char *key)
{
    char ck[80];
    snprintf(ck, sizeof(ck), "%u/%s", (unsigned)h, key);
    int free_i = -1;
    for (int i = 0; i < MAX_ENT; i++) {
        if (s_ent[i].used && strcmp(s_ent[i].k, ck) == 0) return &s_ent[i];
        if (!s_ent[i].used && free_i < 0) free_i = i;
    }
    if (free_i < 0) return NULL;
    strncpy(s_ent[free_i].k, ck, sizeof(s_ent[0].k) - 1);
    s_ent[free_i].used = 1;
    return &s_ent[free_i];
}

static ent_t *find(nvs_handle_t h, const char *key)
{
    char ck[80];
    snprintf(ck, sizeof(ck), "%u/%s", (unsigned)h, key);
    for (int i = 0; i < MAX_ENT; i++)
        if (s_ent[i].used && strcmp(s_ent[i].k, ck) == 0) return &s_ent[i];
    return NULL;
}

esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) { ent_t *e = find_or_add(h, k); if (!e) return ESP_FAIL; e->v = v; return ESP_OK; }
esp_err_t nvs_set_u8 (nvs_handle_t h, const char *k, uint8_t  v) { ent_t *e = find_or_add(h, k); if (!e) return ESP_FAIL; e->v = v; return ESP_OK; }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t  v) { ent_t *e = find_or_add(h, k); if (!e) return ESP_FAIL; e->v = (uint32_t)v; return ESP_OK; }

esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *o) { ent_t *e = find(h, k); if (!e) return ESP_ERR_NVS_NOT_FOUND; *o = e->v;            return ESP_OK; }
esp_err_t nvs_get_u8 (nvs_handle_t h, const char *k, uint8_t  *o) { ent_t *e = find(h, k); if (!e) return ESP_ERR_NVS_NOT_FOUND; *o = (uint8_t)e->v;  return ESP_OK; }
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t  *o) { ent_t *e = find(h, k); if (!e) return ESP_ERR_NVS_NOT_FOUND; *o = (int32_t)e->v;  return ESP_OK; }

esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t len)
{
    if (len > sizeof(((ent_t *)0)->blob)) return ESP_FAIL;
    ent_t *e = find_or_add(h, k);
    if (!e) return ESP_FAIL;
    memcpy(e->blob, v, len);
    e->blob_len = len;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *o, size_t *len)
{
    ent_t *e = find(h, k);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < e->blob_len) return ESP_FAIL;
    memcpy(o, e->blob, e->blob_len);
    *len = e->blob_len;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
{
    ent_t *e = find(h, k);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    memset(e, 0, sizeof(*e));
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t h)
{
    char pre[16];
    int n = snprintf(pre, sizeof(pre), "%u/", (unsigned)h);
    for (int i = 0; i < MAX_ENT; i++)
        if (s_ent[i].used && strncmp(s_ent[i].k, pre, (size_t)n) == 0) memset(&s_ent[i], 0, sizeof(s_ent[i]));
    return ESP_OK;
}

/* Test hook: the next nvs_commit fails once (models any commit-path failure; a full flash on the
 * device fails earlier, at nvs_set_i32, but the caller sees the same error). */
static int s_fail_next_commit = 0;
void sim_nvs_fail_next_commit(void) { s_fail_next_commit = 1; }

esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (s_fail_next_commit) { s_fail_next_commit = 0; return ESP_FAIL; }
    const char *d = getenv("SIM_NVS_COMMIT_DELAY_MS");
    int ms = d ? atoi(d) : 0;
    fprintf(stderr, "[nvs] commit begin (delay %dms)\n", ms);
    if (ms > 0) {
        struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    fprintf(stderr, "[nvs] commit end\n");
    return ESP_OK;
}
