#include "pomodoist_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void make_page(pomo_tasklist_t *page, int count, int first_id)
{
    memset(page, 0, sizeof(*page));
    for (int i = 0; i < count; i++) {
        snprintf(page->task[i].id, sizeof(page->task[i].id), "%d", first_id + i);
        page->count++;
    }
}

int main(void)
{
    pomo_core_t core;
    pomo_core_init(&core);
    core.tasks.count = 3;
    core.active = core.cursor = 1;
    core.left_s = 18 * 60;
    core.running = true;

    pomo_core_select(&core, 2);
    assert(core.left_s == 18 * 60);
    assert(core.running);
    pomo_core_reset(&core);
    assert(core.left_s == core.total_s);
    assert(!core.running);
    assert(core.active == 2 && core.cursor == 2);

    core.cursor = 0;
    pomo_core_cursor_move(&core, -1);
    assert(core.cursor == 2);
    pomo_core_cursor_move(&core, 1);
    assert(core.cursor == 0);

    // length nudge: idle only, clamped to the core's own 5..60
    pomo_core_t n;
    pomo_core_init(&n);
    pomo_core_nudge_minutes(&n, 100);
    assert(n.total_s == POMO_MAX_MINUTES * 60 && n.left_s == n.total_s);
    pomo_core_nudge_minutes(&n, -100);
    assert(n.total_s == POMO_MIN_MINUTES * 60);
    n.running = true;
    pomo_core_nudge_minutes(&n, 5);
    assert(n.total_s == POMO_MIN_MINUTES * 60);

    const char utf8[] = {
        'A', 0xE2, 0x80, 0x94, 'B', 0xC2, 0xA0, 0xE2, 0x80, 0x9C,
        'o', 'k', 0xE2, 0x80, 0x9D, 0xE2, 0x80, 0xA6, '\0'
    };
    char text[32];
    pomo_task_text_copy(text, sizeof(text), utf8);
    assert(strcmp(text, "A-B \"ok\"...") == 0);

    pomo_tasklist_t first;
    pomo_tasklist_t second;
    pomo_tasklist_t combined;
    make_page(&first, 16, 100);
    make_page(&second, 10, 116);
    memset(&combined, 0, sizeof(combined));
    assert(pomo_tasklist_append(&combined, &first) == 16);
    assert(pomo_tasklist_append(&combined, &second) == 10);
    assert(combined.count == 26);
    assert(strcmp(combined.task[0].id, "100") == 0);
    assert(strcmp(combined.task[25].id, "125") == 0);

    puts("pomodoist_core: ok");

    // A2/A3 fields
    assert(POMO_TITLE_LEN == 80);
    {
        pomo_core_t c; pomo_core_init(&c);
        c.tasks.count = 3;
        c.tasks.task[0].pomos = 3; c.tasks.task[0].done = false;
        c.tasks.task[1].pomos = 2; c.tasks.task[1].done = true;    // done tasks owe nothing
        c.tasks.task[2].pomos = 0; c.tasks.task[2].done = false;   // unplanned owes nothing
        uint8_t done_today[POMO_MAX_TASKS] = { 1, 0, 0 };
        assert(pomo_core_pomos_left(&c, done_today) == 2);
        done_today[0] = 5;                                          // over-delivered clamps at 0
        assert(pomo_core_pomos_left(&c, done_today) == 0);
        assert(pomo_core_pomos_left(&c, NULL) == 3);                // NULL = nothing done yet
    }
    // C12 eta: 25 min focus, 5 short, 15 long every 4th.
    assert(pomo_core_eta_s(0, 1500, 300, 900) == 0);
    assert(pomo_core_eta_s(1, 1500, 300, 900) == 1500);                 // last block has no trailing break
    assert(pomo_core_eta_s(2, 1500, 300, 900) == 1500 + 300 + 1500);
    assert(pomo_core_eta_s(4, 1500, 300, 900) == 4 * 1500 + 3 * 300);   // long break only AFTER the 4th
    assert(pomo_core_eta_s(5, 1500, 300, 900) == 5 * 1500 + 3 * 300 + 900);
    printf("core: all asserts passed\n");

    return 0;
}
