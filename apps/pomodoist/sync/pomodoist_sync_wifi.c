// pomodoist_sync_wifi.c — todoist-sync transport B (on-device wifi fetch + durable write-back).
//
// Shaped around the disk's actual day: online at a desk in the morning, offline in a bag at
// work, online again at night. So the sync task is a permanent loop, not a boot-time errand.
//
//   wait for the link trev_net owns (it walks home, then the phone hotspot; ADR-0018)
//   -> flush the NVS outbox (every pomo finished while offline, oldest first)
//   -> fetch projects, fetch today's tasks, resolve project names, cache to NVS, publish
//   -> stay associated; refetch hourly, flush whenever a pomo lands
//   -> on disconnect, wait for the link again
//
// This file never brings Wi-Fi up (T1, E7, C1): trev_net owns the STA netif, the SSID walk and
// SNTP. Here we only listen for GOT_IP / STA_DISCONNECTED and set our own bits.
//
// The app never waits on any of this: it loads the NVS cache at boot and keeps working with
// no wifi all day. Completions go into the outbox (pomodoist_outbox.c) the instant they
// happen and survive reboots, so a whole offline day flushes in order on the next connect.
#include "pomodoist_sync.h"
#include "pomodoist_sync_internal.h"
#include "pomodoist_parse.h"
#include "pomodoist_outbox.h"
#include "trev_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"   // nvs_flash_init_partition, for the D18 cache partition
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR (no-op unless SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// E7: the compile-time half of "one Wi-Fi owner". Nothing here may create the STA netif; a
// second netif aborts at boot (C1). ponytail: a flag no build sets, kept so a paste of the old
// bring-up fails loudly at build time; trev_net_start's second-call refusal is the runtime twin.
#define POMO_OWNS_WIFI 0
#if POMO_OWNS_WIFI
#error "trev_net owns Wi-Fi (ADR-0018); pomodoist must not create the STA netif"
#endif

static const char *TAG = "pomo_wifi";
#define NVS_NS      "pomodoist"
#define NVS_KEY      "tasks"
#define NVS_KEY_MAP  "projmap"

#define GOT_IP_BIT       BIT1   // STA has an IP
#define DISCONNECTED_BIT BIT2   // STA dropped (or failed to associate)
#define FLUSH_BIT        BIT3   // a pomo landed in the outbox; drain it now

#define REFRESH_MS       (60 * 60000)   // hourly refetch while associated

// ADR-0017 kill criterion: on the CYD, read the heap at every step a fetch can fail on. Only
// where a board asks for the ledger, so other boards' logs stay as they are.
static void log_heap(const char *when);
#ifdef TT_HEAP_LEDGER
#define LEDGER(when) log_heap(when)
#else
#define LEDGER(when) ((void)0)
#endif


// Master gate for ALL device->Todoist writes. Default ON; set to 0 at build time
// (e.g. -DTT_TODOIST_WRITE=0, or override here) for safe bring-up with zero live POSTs.
// It gates the POST only: completions still land in the outbox, because the outbox is also
// the local ledger of what was finished today.
#ifndef TT_TODOIST_WRITE
#define TT_TODOIST_WRITE 1
#endif

static char  s_token[64];
static EventGroupHandle_t s_eg;
static volatile bool s_connected;         // STA has an IP right now; read from the LVGL thread
static time_t s_last_sync;                // wall time of the last successful publish; 0 = never
// D1 runtime kill-switch, layered on top of the TT_TODOIST_WRITE compile gate. It lives here
// rather than in main.c because it gates the POST only: a completion is always recorded in
// the outbox, which is also the local ledger the C11 face and the C12 day arithmetic read.
static bool   s_write_enabled = (TT_TODOIST_WRITE != 0);

// ---------------------------------------------------------------- NVS cache

// Two blobs live in this namespace: the task list and the project id -> name map. Both are
// fixed-size structs, so a size mismatch on read means a stale layout and the blob is dropped.
//
// WHICH PARTITION (D18). A board that defines POMO_CACHE_PARTITION puts these two blobs in a
// partition of their own; everyone else keeps using the main `nvs`, unchanged. The disk needs
// this because raising POMO_DESC_LEN to 480 took sizeof(pomo_tasklist_t) from 9,700 B to
// 21,220 B, and the main `nvs` is 0x6000 = 24 KB TOTAL, already holding the outbox rings, the
// project map, the timer and settings blobs and the wifi config, with a page held back for
// compaction. The blob simply does not fit, and until this commit nothing said so: blob_save
// ignored both return codes, so the fetch logged "(cached to NVS)" and the next offline
// morning came up empty. The outbox stays in the main `nvs` deliberately - it is small, and a
// finished pomo must survive independently of a cache that is disposable by design.
#ifdef POMO_CACHE_PARTITION
// s_cache_part: the partition mounted. s_cache_tried: we have attempted the mount, so the
// result (either way) is final and the log line is not repeated. False s_cache_part means we
// are falling back to the main nvs, which for a short task list still works.
static bool s_cache_part, s_cache_tried;

// THE MOUNT IS LAZY, AND THAT IS LOAD-BEARING. pomodoist_sync_cache_load() is called from
// app_main to restore the task list at boot, ~112 lines BEFORE pomodoist_sync_wifi_start()
// starts the transport - so a mount that only happened in start left the boot-time read
// pointing at the main nvs, which no longer holds the list, and every offline boot came up with
// an empty task list while the later save went to `cache` correctly.
static void cache_partition_init(void);

static esp_err_t cache_open(nvs_open_mode_t mode, nvs_handle_t *h)
{
    cache_partition_init();   // idempotent: whichever of load/save runs first does the mount
    if (s_cache_part) return nvs_open_from_partition(POMO_CACHE_PARTITION, NVS_NS, mode, h);
    return nvs_open(NVS_NS, mode, h);
}

// Runs once, on the first blob access from either direction. A blank partition is formatted by
// NVS itself, so the erase-and-retry is only for a full or corrupted one - without it a single
// bad page would disable the cache permanently, which is exactly the silent failure this whole
// change is about.
static void cache_partition_init(void)
{
    if (s_cache_tried) return;
    s_cache_tried = true;
    esp_err_t e = nvs_flash_init_partition(POMO_CACHE_PARTITION);
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "cache partition '%s' unusable (%s); erasing and retrying",
                 POMO_CACHE_PARTITION, esp_err_to_name(e));
        nvs_flash_erase_partition(POMO_CACHE_PARTITION);
        e = nvs_flash_init_partition(POMO_CACHE_PARTITION);
    }
    s_cache_part = (e == ESP_OK);
    if (s_cache_part) ESP_LOGI(TAG, "task cache on partition '%s'", POMO_CACHE_PARTITION);
    else ESP_LOGW(TAG, "cache partition '%s' failed (%s); falling back to the main nvs, where a "
                       "%u byte task list will NOT fit", POMO_CACHE_PARTITION,
                  esp_err_to_name(e), (unsigned)sizeof(pomo_tasklist_t));
}
#else
static esp_err_t cache_open(nvs_open_mode_t mode, nvs_handle_t *h) { return nvs_open(NVS_NS, mode, h); }
static void cache_partition_init(void) { }
#endif

static bool blob_load(const char *key, void *p, size_t size)
{
    nvs_handle_t h;
    if (cache_open(NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = size;
    esp_err_t e = nvs_get_blob(h, key, p, &sz);
    nvs_close(h);
    return e == ESP_OK && sz == size;
}

// Returns whether the blob is actually on flash. It used to return void and discard both error
// codes, which is how a cache that could never be written reported success for a whole sprint.
// A caller that tells the user "cached" MUST look at this.
static bool blob_save(const char *key, const void *p, size_t size)
{
    nvs_handle_t h;
    esp_err_t e = cache_open(NVS_READWRITE, &h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "cache save '%s' (%u B): open failed, %s", key, (unsigned)size, esp_err_to_name(e));
        return false;
    }
    e = nvs_set_blob(h, key, p, size);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK)
        ESP_LOGW(TAG, "cache save '%s' (%u B) FAILED: %s", key, (unsigned)size, esp_err_to_name(e));
    return e == ESP_OK;
}

bool pomodoist_sync_cache_load(pomo_tasklist_t *out)
{
    bool ok = blob_load(NVS_KEY, out, sizeof(*out));
    // Said out loud at boot because the offline day depends on it and the old silence made an
    // empty glass indistinguishable from a device that had simply never synced.
    if (ok) ESP_LOGI(TAG, "cache: %d tasks restored", out->count);
    else    ESP_LOGI(TAG, "cache: empty");
    return ok;
}

// ---------------------------------------------------------------- wifi

// Events only set bits. trev_net owns every connect and reconnect; this second handler on the
// same events just mirrors the link into this file's own bits (two handlers per event are fine
// in the default loop).
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_eg, GOT_IP_BIT);
        xEventGroupSetBits(s_eg, DISCONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        LEDGER("got-ip");
        xEventGroupClearBits(s_eg, DISCONNECTED_BIT);
        xEventGroupSetBits(s_eg, GOT_IP_BIT);
    }
}

// ---------------------------------------------------------------- HTTPS

typedef struct { char *buf; int len, cap; bool over; } acc_t;

// Hard ceiling on any single response body. The `limit=` in the URL is a REQUEST, not a
// memory boundary — the server decides what it actually sends, and this accumulator doubles
// on every overflow. On 67KB of DRAM an unbounded body is an OOM waiting for a bad day, so
// the trust boundary gets an explicit cap. An 11-task page measures 10.4KB on this account,
// leaving headroom below the cap and under the largest free block.
#define HTTP_BODY_MAX (16 * 1024)
#define TODOIST_MAX_PAGES 3

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        acc_t *a = e->user_data;
        if (a->len + e->data_len + 1 > HTTP_BODY_MAX) {
            // Returning ESP_FAIL does not stop the client delivering the remaining chunks, so
            // log once and stay quiet — the first version of this guard emitted 17 identical
            // lines per response.
            if (!a->over) {
                a->over = true;
                ESP_LOGE(TAG, "response exceeds %d byte cap at %d bytes — aborting", HTTP_BODY_MAX, a->len);
            }
            return ESP_FAIL;
        }
        if (a->len + e->data_len + 1 > a->cap) {
            int nc = (a->len + e->data_len + 1) * 2;
            if (nc > HTTP_BODY_MAX) nc = HTTP_BODY_MAX;   // never overshoot the cap
            char *nb = realloc(a->buf, nc);
            if (!nb) return ESP_FAIL;
            a->buf = nb; a->cap = nc;
        }
        memcpy(a->buf + a->len, e->data, e->data_len);
        a->len += e->data_len;
        a->buf[a->len] = 0;
    }
    return ESP_OK;
}

// returns malloc'd body (caller frees) or NULL
static char *https_get(const char *url)
{
    acc_t a = {0};
    char auth[96];
    snprintf(auth, sizeof auth, "Bearer %s", s_token);
    esp_http_client_config_t c = {
        .url = url,
        .event_handler = on_http,
        .user_data = &a,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    esp_http_client_set_header(h, "Authorization", auth);
    LEDGER("pre-get");
    esp_err_t e = esp_http_client_perform(h);
    int code = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    LEDGER("after-get");
    if (a.over || e != ESP_OK || code != 200) {
        ESP_LOGW(TAG, "GET failed err=%d code=%d%s", (int)e, code,
                 a.over ? " response-too-large" : "");
        free(a.buf);
        return NULL;
    }
    if (!a.buf || a.len == 0) {   // 200 with nothing in it reads identically to a parse bug
        ESP_LOGW(TAG, "GET returned 200 but an empty body: %s", url);
        free(a.buf);
        return NULL;
    }
    return a.buf;
}

#if TT_TODOIST_WRITE
// POST a JSON body to the Todoist v1 API. Mirrors https_get: Bearer auth + cert bundle,
// status-checked, frees everything. Returns the HTTP status, or -1 when the request never
// got far enough to have one. The caller needs the number rather than a bool: a 404 is a
// deleted task to retire, and every other failure is a reason to stop the flush.
static int https_post(const char *url, const char *body)
{
    acc_t a = {0};
    char auth[96];
    snprintf(auth, sizeof auth, "Bearer %s", s_token);
    esp_http_client_config_t c = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http,
        .user_data = &a,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    esp_http_client_set_header(h, "Authorization", auth);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    if (body && body[0]) esp_http_client_set_post_field(h, body, (int)strlen(body));
    LEDGER("pre-post");
    esp_err_t e = esp_http_client_perform(h);
    int code = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    LEDGER("after-post");
    bool ok = (e == ESP_OK && code >= 200 && code < 300);
    // The response body is the only place Todoist says WHY: a dead token, a deleted task and
    // a malformed comment all arrive as bare 4xx otherwise, and the outbox item that keeps
    // failing is exactly the one worth explaining. 200 chars covers every error it returns.
    if (!ok)
        ESP_LOGW(TAG, "POST failed err=%d code=%d url=%s body=%.200s",
                 (int)e, code, url, a.buf ? a.buf : "(none)");
    free(a.buf);
    return (e == ESP_OK) ? code : -1;
}
#endif

// T11: current-free alone hid the real story. A fetch can fail on fragmentation with plenty
// of total heap, so log the LARGEST contiguous block (what a single malloc can actually get)
// and the min-ever watermark (how close the worst moment came) alongside it.
static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %-12s free=%u largest=%u min_ever=%u", when,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)esp_get_minimum_free_heap_size());
}

// ---------------------------------------------------------------- outbox flush

#if TT_TODOIST_WRITE
// One comment per completed pomodoro, on the real task id. A6 spike, verified live
// 2026-09-03: POST /comments on an ALREADY-COMPLETED task returns 200, so there is no
// ledger-task fallback to write to and nothing to redirect — always post to the task itself.
static int post_pomo(const outbox_item_t *it)
{
    char text[POMO_DESC_LEN];
    time_t t = (time_t)it->ended_utc;
    struct tm tm;
    localtime_r(&t, &tm);
    // A pomo finished on an offline, never-synced device carries a 1970 timestamp. Saying so
    // is honest; dating the comment 1970-01-01 in Todoist is not.
    if (tm.tm_year + 1900 < 2020)
        snprintf(text, sizeof text, "\xF0\x9F\x8D\x85 Pomo complete (%u min) - (clock unsynced)",
                 (unsigned)it->minutes);
    else
        snprintf(text, sizeof text, "\xF0\x9F\x8D\x85 Pomo complete (%u min) - %04d-%02d-%02d %02d:%02d",
                 (unsigned)it->minutes, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "task_id", it->task_id);
    cJSON_AddStringToObject(o, "content", text);   // cJSON escapes the emoji and any quotes
    char *body = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!body) return -1;
    int code = https_post("https://api.todoist.com/api/v1/comments", body);
    free(body);
    ESP_LOGI(TAG, "comment on %s: status %d", it->task_id, code);
    return code;
}
#endif

// Drain the outbox oldest-first. Called on every connect, on every finished pomo while
// connected, and hourly. Stops at the first failure and keeps the item: a dead token or a
// flaky link is not a reason to throw away a real pomodoro, and the item is retried on the
// next connect. The one exception is a 404, below.
// pomodoist_outbox.c takes its own lock, so nothing is held across a POST here.
static void flush_outbox(void)
{
    outbox_item_t it;
    int sent = 0;
#if TT_TODOIST_WRITE
    if (!s_write_enabled) {
        // Same posture as writes-compiled-out: keep every item queued. The switch is meant to
        // be flipped back on, and a pomo thrown away here would never come back.
        ESP_LOGI(TAG, "outbox holds %d item(s); writes disabled in Settings", outbox_count());
        return;
    }
#endif
    while (outbox_peek(&it)) {
#if TT_TODOIST_WRITE
        // A previous connection may have POSTed this and lost power before the pop landed.
        // The sent ring is the dedupe log, and a duplicate comment is noise the user sees.
        if (!outbox_already_sent(it.task_id, it.ended_utc)) {
            int code = post_pomo(&it);
            if (code == 404) {
                // The task no longer exists in Todoist. Retrying it forever would block
                // everything queued behind it until the ring fills and real completions start
                // being dropped at the push, so this one item is retired and the flush carries
                // on. Every other failure still stops the flush and keeps its item.
                ESP_LOGW(TAG, "task %s is gone (404); retiring its queued pomo", it.task_id);
            } else if (code < 200 || code >= 300) {
                ESP_LOGW(TAG, "flush stopped on status %d, %d item(s) still queued",
                         code, outbox_count());
                break;
            } else {
                sent++;
            }
        }
        outbox_pop_sent();
#else
        (void)it;
        ESP_LOGI(TAG, "outbox holds %d item(s); writes compiled out", outbox_count());
        break;
#endif
    }
    if (sent) ESP_LOGI(TAG, "outbox: %d comment(s) posted", sent);
}

// ---------------------------------------------------------------- fetch

static bool append_url_escaped(char *dst, size_t cap, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t written = strlen(dst);
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        size_t need = safe ? 1 : 3;
        if (written + need >= cap) return false;
        if (safe) dst[written++] = (char)c;
        else {
            dst[written++] = '%';
            dst[written++] = hex[c >> 4];
            dst[written++] = hex[c & 0x0f];
        }
    }
    dst[written] = '\0';
    return true;
}

static bool build_url(char *url, size_t cap, const char *base, const char *cursor)
{
    int written = snprintf(url, cap, "%s%s", base, cursor[0] ? "&cursor=" : "");
    return written >= 0 && (size_t)written < cap &&
           (!cursor[0] || append_url_escaped(url, cap, cursor));
}

// Today plus anything overdue, filtered server-side so the device needs no clock to ask.
static bool fetch_tasks(pomo_tasklist_t *out, char proj_id[POMO_MAX_TASKS][POMO_ID_LEN])
{
    static const char base[] =
        "https://api.todoist.com/api/v1/tasks/filter?query=today%20%7C%20overdue&limit=11";
    char cursor[128] = "";
    for (int page = 0; page < TODOIST_MAX_PAGES && out->count < POMO_MAX_TASKS; page++) {
        char url[384], next[sizeof cursor] = "";
        if (!build_url(url, sizeof url, base, cursor)) { ESP_LOGE(TAG, "tasks URL too long"); return false; }
        char *body = https_get(url);
        if (!body) { ESP_LOGW(TAG, "tasks GET returned no body"); return false; }
        ESP_LOGI(TAG, "tasks body=%u bytes", (unsigned)strlen(body));
        bool ok = pomo_parse_task_page(body, out, proj_id, next, sizeof next);
        LEDGER("tasks-parse");
        free(body);
        if (!ok) return false;
        if (!next[0]) break;
        if (page == TODOIST_MAX_PAGES - 1) ESP_LOGW(TAG, "task list capped at %d", POMO_MAX_TASKS);
        strcpy(cursor, next);
    }
    return true;
}

// The task payload carries project_id, never the project name, so the names come from a
// second endpoint and are joined locally. Best-effort: a failed project fetch costs the
// labels on the glass, not the task list.
static bool fetch_projects(pomo_projmap_t *map)
{
    // MEASURED on the live account 2026-09-03: a project object is about 840 bytes, so
    // ?limit=100 returns 24,341 bytes and ?limit=20 still returns 16,743 - both over
    // HTTP_BODY_MAX, which makes https_get return NULL and blanks every project name. 12 per
    // page is about 10 KB, the same size as the 11-task page this file already sized for.
    // Raising the cap instead is not an option: the CYD has no PSRAM.
    static const char base[] = "https://api.todoist.com/api/v1/projects?limit=12";
    char cursor[128] = "";
    // 4 x 12 = 48 = POMO_MAX_PROJ, so the whole map is reachable.
    for (int page = 0; page < 4 && map->count < POMO_MAX_PROJ; page++) {
        char url[384], next[sizeof cursor] = "";
        if (!build_url(url, sizeof url, base, cursor)) { ESP_LOGE(TAG, "projects URL too long"); return false; }
        char *body = https_get(url);
        if (!body) { ESP_LOGW(TAG, "projects GET returned no body"); return false; }
        bool ok = pomo_parse_projects(body, map, next, sizeof next);
        LEDGER("proj-parse");
        free(body);
        if (!ok) return false;
        if (!next[0]) break;
        strcpy(cursor, next);
    }
    return true;
}

// One full refresh: projects, then tasks, then join, cache and publish.
static void refresh(void)
{
    // static: ~9KB that would otherwise blow this task's stack, and only one task ever runs it.
    // PSRAM .bss on the disk (D18 grew this to 21 KB and internal DRAM ran out for TLS and httpd); no-op elsewhere
    EXT_RAM_BSS_ATTR static pomo_tasklist_t l;
    static char proj_id[POMO_MAX_TASKS][POMO_ID_LEN];
    // PSRAM .bss on the disk (D18 grew this to 21 KB and internal DRAM ran out for TLS and httpd); no-op elsewhere
    EXT_RAM_BSS_ATTR static pomo_projmap_t map;

    log_heap("pre-fetch");
    memset(&l, 0, sizeof l);
    memset(proj_id, 0, sizeof proj_id);
    memset(&map, 0, sizeof map);

    // The map is cached in its own NVS blob, not just baked into the task list, because a
    // transient projects failure would otherwise blank every project name on the glass for
    // the rest of the offline day. A failed fetch reuses the last good map and never
    // overwrites it; if there has never been one, tasks still publish with blank projects,
    // because blank projects beat no tasks.
    if (fetch_projects(&map)) {
        blob_save(NVS_KEY_MAP, &map, sizeof map);
    } else if (blob_load(NVS_KEY_MAP, &map, sizeof map)) {
        ESP_LOGW(TAG, "projects fetch failed; using the cached map (%d projects)", map.count);
    } else {
        memset(&map, 0, sizeof map);   // a partial page may have landed before the failure
        ESP_LOGW(TAG, "projects fetch failed and nothing cached; names blank this round");
    }
    if (!fetch_tasks(&l, proj_id)) { ESP_LOGW(TAG, "fetch failed; keeping cache"); return; }
    pomo_apply_project_names(&l, proj_id, &map);

    // Publish regardless of the save: a fetched list is good for this session even if it will
    // not survive the reboot. But do not CLAIM it was cached unless it was.
    bool cached = blob_save(NVS_KEY, &l, sizeof l);
    pomo_sync_publish(&l);
    s_last_sync = time(NULL);
    log_heap("tasks-parsed");
    ESP_LOGI(TAG, "synced %d tasks / %d projects from Todoist %s", l.count, map.count,
             cached ? "(cached to NVS)" : "(cache save FAILED, see warning)");
}

// ---------------------------------------------------------------- the loop

static void wifi_task(void *arg)
{
    (void)arg;
    if (trev_net_connected()) {   // the link came up before our handler existed; do not wait for an event that already fired
        s_connected = true;
        xEventGroupSetBits(s_eg, GOT_IP_BIT);
    }

    for (;;) {
        // Offline is the normal state all day at work. This blocks the task outright rather
        // than polling, so the idle CPU cost of being offline is zero.
        xEventGroupWaitBits(s_eg, GOT_IP_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

        flush_outbox();   // before the fetch: yesterday's pomos are older news than today's list
        refresh();

        // Stay associated: staying up is the rail the hourly refetch and the write-back both
        // ride on. Two things wake us: a finished pomo (FLUSH_BIT) and the hourly refetch
        // deadline.
        TickType_t next_refresh = xTaskGetTickCount() + pdMS_TO_TICKS(REFRESH_MS);
        for (;;) {
            TickType_t now = xTaskGetTickCount();
            TickType_t wait = (int32_t)(next_refresh - now) > 0 ? next_refresh - now : 0;
            EventBits_t b = xEventGroupWaitBits(s_eg, DISCONNECTED_BIT | FLUSH_BIT,
                                                pdTRUE, pdFALSE, wait);
            if (b & DISCONNECTED_BIT) { ESP_LOGW(TAG, "wifi dropped; waiting for the link"); break; }
            if (b & FLUSH_BIT) flush_outbox();
            // Signed tick difference, so this stays correct across the tick counter wrapping.
            if ((int32_t)(xTaskGetTickCount() - next_refresh) >= 0) {
                flush_outbox();
                refresh();
                next_refresh = xTaskGetTickCount() + pdMS_TO_TICKS(REFRESH_MS);
            }
        }
    }
}

// ---------------------------------------------------------------- public API

// Call BEFORE trev_net_start (C1): the handlers must exist when the first GOT_IP fires. The
// default event loop is main.c's, created before this.
void pomodoist_sync_wifi_start(const char *token)
{
    if (token) strncpy(s_token, token, sizeof s_token - 1);

    s_eg = xEventGroupCreate();   // before the handlers: they set bits in it
    if (!s_eg) { ESP_LOGE(TAG, "no event group; transport B not started"); return; }
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL);

    // Before the task exists, so a pomo finished in the first seconds of the boot has
    // somewhere durable to land and the flush sees a loaded ring. outbox_init also creates
    // the outbox's own lock, which is what makes it safe to call from the LVGL thread.
    outbox_init();
    // D18: harmless by design now that the mount is lazy (cache_partition_init is idempotent).
    // Kept so the "task cache on partition" line lands next to the outbox count at start-up
    // rather than wherever the first blob access happens to be - but nothing depends on it,
    // and the boot-time restore in app_main has already mounted it by the time we get here.
    cache_partition_init();
    ESP_LOGI(TAG, "transport B starting, %d item(s) in the outbox", outbox_count());
    xTaskCreate(wifi_task, "pomo_wifi", 8192, NULL, 5, NULL);
}

bool pomodoist_sync_connected(void) { return s_connected; }

bool pomodoist_sync_last_sync(time_t *out)
{
    if (out) *out = s_last_sync;
    return s_last_sync != 0;
}

// See the header: "trusted" is not the same question as "the clock is believable", and a board
// with an RTC can only tell them apart from here. Deep sleep is a reboot, so this resets on
// every wake and answers about THIS boot, which is the honest scope.
bool pomodoist_sync_clock_synced(void)
{
    return trev_net_time_state() == TREV_TIME_TRUSTED;
}

void pomodoist_sync_log_pomo(const char *task_id, int64_t ended_utc, uint16_t minutes)
{
    if (!task_id || !task_id[0]) return;   // B1: serial-pushed tasks carry no id, nothing to write back to

    // I2: on a cold boot with a flat coin cell and no SNTP, time(NULL) restarts near zero and
    // just counts uptime, so two pomos on the same task at the same uptime offset across two
    // boots produce an IDENTICAL dedupe key (task_id + ended_utc) and the second is silently
    // dropped at flush. Stamp a negative per-boot sentinel instead: unique within a boot
    // (uptime seconds) and across boots (the random salt), excluded from the ledger by its
    // [day0, day0+86400) filter, and rendered "(clock unsynced)" by post_pomo, which is the
    // honest reading anyway.
    if (ended_utc < 1577836800) {   // pre-2020: the clock was never synced this boot
        static int64_t s_boot_salt;
        if (!s_boot_salt) s_boot_salt = (int64_t)(esp_random() & 0xFFFF) + 1;
        ended_utc = -((int64_t)(esp_timer_get_time() / 1000000) + s_boot_salt);
    }

    outbox_item_t it = {0};
    strncpy(it.task_id, task_id, sizeof it.task_id - 1);
    it.ended_utc = ended_utc;
    it.minutes = minutes;

    // This is called from the LVGL thread and commits to NVS, so it costs a few ms of UI
    // time. That is the deliberate trade for durability: the old RAM queue lost the whole
    // day's completions on any reboot, and the disk spends its day offline. The outbox takes
    // its own lock, so this is safe against the wifi task draining at the same moment.
    if (!outbox_push(&it)) {
        // The ring is full, which means a real pomodoro is being thrown away. Say so at ERROR
        // with everything needed to re-enter it by hand; the outbox's own WARN does not name
        // the loss from the sync layer's point of view.
        ESP_LOGE(TAG, "outbox FULL, POMO LOST: task=%s %u min ended=%lld",
                 it.task_id, (unsigned)minutes, (long long)ended_utc);
        return;
    }

    if (s_connected && s_eg) xEventGroupSetBits(s_eg, FLUSH_BIT);   // online: go now, do not wait for the hour
}

void pomodoist_sync_set_write(bool enabled) { s_write_enabled = enabled; }
