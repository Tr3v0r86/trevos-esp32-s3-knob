#include "pomodoist_outbox.h"
#include "nvs_flash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int n_seen; static void count_cb(const outbox_item_t *it, bool sent, void *ctx) { (void)it; (void)sent; (void)ctx; n_seen++; }

int main(void)
{
    nvs_flash_erase(); nvs_flash_init();
    outbox_init();
    assert(outbox_count() == 0);
    outbox_item_t a = { "task-A", 1000, 25 }, b = { "task-B", 2000, 5 };
    assert(outbox_push(&a)); assert(outbox_push(&b));
    assert(outbox_count() == 2);

    outbox_init();                       // simulate reboot: reload from NVS
    assert(outbox_count() == 2);
    outbox_item_t p; assert(outbox_peek(&p)); assert(strcmp(p.task_id, "task-A") == 0);   // FIFO

    assert(!outbox_already_sent("task-A", 1000));
    outbox_pop_sent();
    assert(outbox_count() == 1);
    assert(outbox_already_sent("task-A", 1000));
    assert(!outbox_already_sent("task-A", 1001));

    outbox_init();                       // reboot again: sent ring persisted too
    assert(outbox_already_sent("task-A", 1000));
    assert(outbox_done_today("task-A", 0, 5000) == 1);
    assert(outbox_done_today("task-B", 0, 5000) == 1);   // pending counts as done today
    assert(outbox_done_today("task-B", 3000, 5000) == 0);
    n_seen = 0; outbox_for_each_today(0, 5000, count_cb, NULL); assert(n_seen == 2);

    for (int i = 0; i < OUTBOX_CAP + 2; i++) { outbox_item_t x = { "x", i, 1 }; outbox_push(&x); }
    assert(outbox_count() == OUTBOX_CAP);   // full: push returns false, nothing lost silently

    // Fix round 1: prove the outbox_pop_sent write-order invariant (KEY_SENT before
    // KEY_RING) is safe against a crash between the two writes. Simulate that crash
    // window by re-pushing an already-sent item (same task_id + ended_utc) and confirm
    // the duplicate is harmless: already_sent() still catches it, so the flush layer
    // dedupes the retry instead of double-posting.
    nvs_flash_erase(); nvs_flash_init(); outbox_init();
    outbox_item_t c = { "task-A", 1000, 25 };
    assert(outbox_push(&c));
    outbox_pop_sent();                          // sent + ring both updated for task-A
    assert(outbox_count() == 0);
    assert(outbox_already_sent("task-A", 1000));
    assert(outbox_push(&c));                    // simulate the crash window: re-push the same item
    assert(outbox_already_sent("task-A", 1000));
    assert(outbox_count() == 1);

    printf("outbox: all asserts passed\n");
    return 0;
}
