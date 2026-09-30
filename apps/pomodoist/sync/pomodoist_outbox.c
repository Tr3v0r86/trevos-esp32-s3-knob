// pomodoist_outbox.c: durable write-back queue (A6 storage half). See the header for
// the contract; this file only holds the two ring structs (as file statics) and the
// NVS load/save around them. Every mutation persists both affected blobs before
// returning, so a reboot mid-day never loses or double-sends a completion.
#include "pomodoist_outbox.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

// The rings are shared state: the wifi task drains them while the LVGL thread pushes a
// finished pomo and reads today's ledger (outbox_done_today / outbox_for_each_today). The
// lock lives HERE rather than in one caller, because every caller has to be safe, not just
// the first one. Recursive, so a for_each_today callback that asks the outbox another
// question does not deadlock.
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_mux;
static void ob_lock(void)   { if (s_mux) xSemaphoreTakeRecursive(s_mux, portMAX_DELAY); }
static void ob_unlock(void) { if (s_mux) xSemaphoreGiveRecursive(s_mux); }
#else
// ponytail: the host test is single-threaded and sim/shims has no FreeRTOS, so off-target
// the lock compiles to nothing rather than dragging a shim in for one mutex.
static void ob_lock(void)   { }
static void ob_unlock(void) { }
#endif

static const char *TAG = "pomo_outbox";
#define NVS_NS   "pomo_out"
#define KEY_RING "ring"
#define KEY_SENT "sent"

typedef struct { char task_id[32]; int64_t ended_utc; } sent_rec_t;

typedef struct { uint8_t head, count; outbox_item_t it[OUTBOX_CAP]; }      ring_t;
typedef struct { uint8_t head, count; sent_rec_t     it[OUTBOX_SENT_CAP]; } sent_ring_t;

static ring_t      s_ring;
static sent_ring_t s_sent;

static bool load_blob(const char *key, void *ptr, size_t size)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = size;
    esp_err_t e = nvs_get_blob(h, key, ptr, &sz);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return false;   // nothing stored yet, empty ring is correct
    if (e != ESP_OK || sz != size) {
        // Covers ESP_ERR_NVS_INVALID_LENGTH (target: buffer too small for what's stored) and
        // any other stored-size mismatch (e.g. a firmware upgrade grew outbox_item_t or a CAP).
        ESP_LOGW(TAG, "%s: stored blob size mismatch (got %u, want %u), starting empty", key, (unsigned)sz, (unsigned)size);
        return false;
    }
    return true;
}

static void save_blob(const char *key, const void *ptr, size_t size)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGW(TAG, "open failed, %s not persisted", key); return; }
    nvs_set_blob(h, key, ptr, size);
    nvs_commit(h);
    nvs_close(h);
}

static bool in_range(int64_t t, int64_t start, int64_t end) { return t >= start && t < end; }

void outbox_init(void)
{
#ifdef ESP_PLATFORM
    if (!s_mux) s_mux = xSemaphoreCreateRecursiveMutex();   // guarded: init runs again on a re-sync
#endif
    ob_lock();
    memset(&s_ring, 0, sizeof(s_ring));
    memset(&s_sent, 0, sizeof(s_sent));
    load_blob(KEY_RING, &s_ring, sizeof(s_ring));
    load_blob(KEY_SENT, &s_sent, sizeof(s_sent));
    ob_unlock();
}

bool outbox_push(const outbox_item_t *it)
{
    ob_lock();
    if (s_ring.count == OUTBOX_CAP) {
        ob_unlock();
        ESP_LOGW(TAG, "outbox full, dropping push for %s", it->task_id);
        return false;
    }
    int idx = (s_ring.head + s_ring.count) % OUTBOX_CAP;
    s_ring.it[idx] = *it;
    s_ring.count++;
    save_blob(KEY_RING, &s_ring, sizeof(s_ring));
    ob_unlock();
    return true;
}

int outbox_count(void)
{
    ob_lock();
    int n = s_ring.count;
    ob_unlock();
    return n;
}

bool outbox_peek(outbox_item_t *out)
{
    ob_lock();
    bool any = s_ring.count > 0;
    if (any) *out = s_ring.it[s_ring.head];
    ob_unlock();
    return any;
}

void outbox_pop_sent(void)
{
    ob_lock();
    if (s_ring.count == 0) { ob_unlock(); return; }   // ponytail: empty ring is a no-op, nothing to move

    outbox_item_t it = s_ring.it[s_ring.head];
    s_ring.head = (s_ring.head + 1) % OUTBOX_CAP;
    s_ring.count--;

    sent_rec_t rec = {0};
    strncpy(rec.task_id, it.task_id, sizeof(rec.task_id) - 1);
    rec.ended_utc = it.ended_utc;

    int idx;
    if (s_sent.count < OUTBOX_SENT_CAP) {
        idx = (s_sent.head + s_sent.count) % OUTBOX_SENT_CAP;
        s_sent.count++;
    } else {
        // full: overwrite the oldest slot (at head), then advance head past it.
        idx = s_sent.head;
        s_sent.head = (s_sent.head + 1) % OUTBOX_SENT_CAP;
    }
    s_sent.it[idx] = rec;

    // Ordering invariant: write KEY_SENT before KEY_RING. A crash between the two writes
    // then leaves the item in BOTH persisted rings (never in neither): the pending ring
    // still has it so it will retry, and already_sent() already marks it delivered, so the
    // flush dedupes the retry instead of losing the pomo. Never swap this order.
    save_blob(KEY_SENT, &s_sent, sizeof(s_sent));
    save_blob(KEY_RING, &s_ring, sizeof(s_ring));
    ob_unlock();
}

bool outbox_already_sent(const char *task_id, int64_t ended_utc)
{
    bool found = false;
    ob_lock();
    for (int i = 0; i < s_sent.count; i++) {
        int idx = (s_sent.head + i) % OUTBOX_SENT_CAP;
        if (s_sent.it[idx].ended_utc == ended_utc && strcmp(s_sent.it[idx].task_id, task_id) == 0) {
            found = true;
            break;
        }
    }
    ob_unlock();
    return found;
}

int outbox_done_today(const char *task_id, int64_t day_start_utc, int64_t day_end_utc)
{
    int n = 0;
    ob_lock();
    for (int i = 0; i < s_ring.count; i++) {
        int idx = (s_ring.head + i) % OUTBOX_CAP;
        if (strcmp(s_ring.it[idx].task_id, task_id) == 0 && in_range(s_ring.it[idx].ended_utc, day_start_utc, day_end_utc))
            n++;
    }
    for (int i = 0; i < s_sent.count; i++) {
        int idx = (s_sent.head + i) % OUTBOX_SENT_CAP;
        if (strcmp(s_sent.it[idx].task_id, task_id) == 0 && in_range(s_sent.it[idx].ended_utc, day_start_utc, day_end_utc))
            n++;
    }
    ob_unlock();
    return n;
}

void outbox_for_each_today(int64_t day_start_utc, int64_t day_end_utc,
                            void (*fn)(const outbox_item_t *, bool sent, void *), void *ctx)
{
    ob_lock();
    for (int i = 0; i < s_ring.count; i++) {
        int idx = (s_ring.head + i) % OUTBOX_CAP;
        if (in_range(s_ring.it[idx].ended_utc, day_start_utc, day_end_utc))
            fn(&s_ring.it[idx], false, ctx);
    }
    for (int i = 0; i < s_sent.count; i++) {
        int idx = (s_sent.head + i) % OUTBOX_SENT_CAP;
        if (in_range(s_sent.it[idx].ended_utc, day_start_utc, day_end_utc)) {
            outbox_item_t tmp = {0};
            strncpy(tmp.task_id, s_sent.it[idx].task_id, sizeof(tmp.task_id) - 1);
            tmp.ended_utc = s_sent.it[idx].ended_utc;
            fn(&tmp, true, ctx);
        }
    }
    ob_unlock();
}
