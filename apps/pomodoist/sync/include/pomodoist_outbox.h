// pomodoist_outbox.h: durable write-back queue (A6 storage half).
//
// Every completed pomo is pushed here BEFORE the app tries to POST it to Todoist, and
// stays until a caller (transport B, task 5) confirms delivery by calling
// outbox_pop_sent(). Both rings live in NVS namespace "pomo_out" so a reboot mid-day
// (or an all-day offline stretch) never loses or double-sends a completion. The sent
// ring is a dedupe log: a retry after a flaky POST checks outbox_already_sent() first.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define OUTBOX_CAP      32   // pending ring depth
#define OUTBOX_SENT_CAP 64   // sent-dedupe ring depth (FIFO-evicting when full)

typedef struct {
    char     task_id[32];
    int64_t  ended_utc;
    uint16_t minutes;
} outbox_item_t;

void outbox_init(void);                                 // loads both rings from NVS ns "pomo_out"
bool outbox_push(const outbox_item_t *it);               // persists before returning; false if full
int  outbox_count(void);                                 // pending count
bool outbox_peek(outbox_item_t *out);                     // oldest pending, false if empty
void outbox_pop_sent(void);                               // oldest pending -> sent ring, both persisted; no-op if empty
bool outbox_already_sent(const char *task_id, int64_t ended_utc);   // dedupe check

// pending + sent, for C11/C12 (today's completion count, offline-safe)
int  outbox_done_today(const char *task_id, int64_t day_start_utc, int64_t day_end_utc);

// C11 ledger: walks pending then sent items whose ended_utc falls in [day_start_utc, day_end_utc)
void outbox_for_each_today(int64_t day_start_utc, int64_t day_end_utc,
                            void (*fn)(const outbox_item_t *, bool sent, void *), void *ctx);
