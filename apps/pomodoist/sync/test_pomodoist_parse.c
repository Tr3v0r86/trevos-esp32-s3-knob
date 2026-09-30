// test_pomodoist_parse.c — host test for the Todoist JSON parse rules. No ESP, no network.
//
// Build + run (cJSON comes from the IDF checkout; the sim shims supply esp_log.h):
/*
 *   cc -std=c11 -Wall -Wextra -Werror -I apps/pomodoist/sync/include -I apps/pomodoist/core/include \
 *      -I sim/shims -I $IDF_PATH/components/json/cJSON \
 *      $IDF_PATH/components/json/cJSON/cJSON.c apps/pomodoist/sync/pomodoist_parse.c \
 *      apps/pomodoist/core/pomodoist_core.c apps/pomodoist/sync/test_pomodoist_parse.c \
 *      -o /tmp/parse_test && /tmp/parse_test
 */
#include "pomodoist_parse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char TASKS[] = "{\"results\":[{\"id\":\"t1\",\"content\":\"Write the very long title that runs well past forty characters to prove wrap\",\"description\":\"d\",\"priority\":4,\"project_id\":\"p1\",\"labels\":[\"deep\"],\"duration\":{\"amount\":50,\"unit\":\"minute\"}},{\"id\":\"t2\",\"content\":\"No duration\",\"priority\":1,\"project_id\":\"p2\",\"labels\":[]}],\"next_cursor\":null}";
static const char PROJS[] = "{\"results\":[{\"id\":\"p1\",\"name\":\"Hub\"},{\"id\":\"p2\",\"name\":\"Ops\"}],\"next_cursor\":null}";

// A day task (unit "day") plans no pomodoros, and a missing/garbage field is rejected
// without losing the task (#34). Also proves an unknown project_id leaves the name empty.
static const char MESSY[] = "{\"results\":[{\"id\":\"t3\",\"content\":\"Day job\",\"priority\":\"high\",\"project_id\":\"nope\",\"duration\":{\"amount\":1,\"unit\":\"day\"}},{\"content\":\"No id at all\",\"duration\":{\"amount\":600,\"unit\":\"minute\"}}],\"next_cursor\":\"c2\"}";

int main(void)
{
    static pomo_tasklist_t l;
    static char proj_id[POMO_MAX_TASKS][POMO_ID_LEN];
    static pomo_projmap_t map;
    char cursor[128] = "sentinel";

    assert(pomo_parse_task_page(TASKS, &l, proj_id, cursor, sizeof cursor));
    assert(l.count == 2);
    assert(cursor[0] == '\0');                       // next_cursor null -> last page
    assert(l.task[0].pomos == 2);                    // 50 min -> ceil(50/25)
    assert(l.task[0].priority == 4);
    assert(strlen(l.task[0].title) > 40);            // 80-char titles survive (task 1)
    assert(strcmp(l.task[0].id, "t1") == 0);
    assert(strcmp(l.task[0].desc, "d") == 0);
    assert(strcmp(l.task[0].tags, "#deep") == 0);
    assert(l.task[1].pomos == 0);                    // no duration key at all
    assert(l.task[1].priority == 1);

    assert(pomo_parse_projects(PROJS, &map, cursor, sizeof cursor));
    assert(map.count == 2);
    pomo_apply_project_names(&l, proj_id, &map);
    assert(strcmp(l.task[0].project, "Hub") == 0);
    assert(strcmp(l.task[1].project, "Ops") == 0);

    // second page appends, and bad fields are rejected field-wise, not task-wise
    assert(pomo_parse_task_page(MESSY, &l, proj_id, cursor, sizeof cursor));
    assert(l.count == 4);
    assert(strcmp(cursor, "c2") == 0);
    assert(l.task[2].pomos == 0);                    // unit "day" plans nothing
    assert(l.task[2].priority == 0);                 // priority was a string -> rejected
    assert(l.task[3].id[0] == '\0');                 // no id -> kept, but not writable back
    assert(l.task[3].pomos == POMO_MAX_POMOS);       // 600 min clamps to the ceiling
    pomo_apply_project_names(&l, proj_id, &map);
    assert(l.task[2].project[0] == '\0');            // unknown project id -> empty name

    assert(!pomo_parse_task_page("{\"results\":", &l, proj_id, cursor, sizeof cursor));  // garbage rejected
    assert(l.count == 4);                            // and the list is untouched

    printf("parse test OK (%d tasks, %d projects)\n", l.count, map.count);
    return 0;
}
