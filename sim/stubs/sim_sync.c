/* sim/stubs/sim_sync.c: host pomodoist_sync. No serial, no wifi, no network.
 *
 * cache_load returns false -> main.c falls through to seed_demo() so faces have tasks.
 * take returns false: no fresh serial list ever arrives.
 * connected()/last_sync() report a fixed, always-synced disk (07:42 today), so the ledger's
 * eyebrow and the OTA health-mark arm both have something real to read.
 * log_pomo() pushes onto the REAL outbox ring when TT_HAS_OUTBOX is on (roundsim only) and
 * is a no-op everywhere else, mirroring a board with no outbox linked: nothing to push onto.
 * set_write is a no-op: the sim never posts anything, so there is no POST to gate.
 */
#include "pomodoist_sync.h"
#include <string.h>
#include <time.h>

#if defined(TT_HAS_OUTBOX) && TT_HAS_OUTBOX
#include "pomodoist_outbox.h"
#endif

void pomodoist_sync_serial_start(void) { }

void pomodoist_sync_wifi_start(const char *token) { (void)token; }

bool pomodoist_sync_take(pomo_tasklist_t *out)       { (void)out; return false; }
bool pomodoist_sync_cache_load(pomo_tasklist_t *out) { (void)out; return false; }

bool pomodoist_sync_connected(void) { return true; }

// Fixed 07:42 LOCAL TODAY rather than a literal epoch, so the ledger's day-bounds check
// (today's local midnight..midnight+86400) always lands this inside the day being shown,
// whatever day the sim happens to run on.
bool pomodoist_sync_last_sync(time_t *out)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = 7;
    tm.tm_min  = 42;
    tm.tm_sec  = 0;
    *out = mktime(&tm);
    return true;
}

void pomodoist_sync_log_pomo(const char *task_id, int64_t ended_utc, uint16_t minutes)
{
#if defined(TT_HAS_OUTBOX) && TT_HAS_OUTBOX
    outbox_item_t it = {0};
    if (task_id) strncpy(it.task_id, task_id, sizeof(it.task_id) - 1);
    it.ended_utc = ended_utc;
    it.minutes = minutes;
    outbox_push(&it);
#else
    (void)task_id; (void)ended_utc; (void)minutes;
#endif
}

void pomodoist_sync_set_write(bool enabled) { (void)enabled; }
