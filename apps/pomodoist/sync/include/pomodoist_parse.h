// pomodoist_parse.h — Todoist v1 JSON -> pomodoist core types.
//
// Split out of the wifi transport so the parse rules can be tested on the host with
// nothing but cJSON and pomodoist/core. Deliberately free of ESP includes beyond
// esp_log.h, which the sim shims satisfy (sim/shims/esp_log.h).
//
// Policy on bad input (#34): a field with a missing or wrong-typed value is REJECTED and
// left zero; the task itself is kept. A half-typed task on the glass beats no task at all.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "pomodoist_core.h"

#define POMO_MAX_PROJ 48   // projects cached for the id -> name map

typedef struct {
    char id[POMO_ID_LEN];
    char name[POMO_PROJ_LEN];
} pomo_proj_t;

typedef struct {
    pomo_proj_t p[POMO_MAX_PROJ];
    int         count;
} pomo_projmap_t;

// Parse one page of GET /api/v1/tasks/filter. Appends to out (out->count is the write
// cursor, so pages accumulate), writes each task's raw project_id into the parallel
// proj_id array at the SAME index, and copies next_cursor ("" when the page is last).
// proj_id is a separate out-param because the project map is fetched independently: the
// names get applied once, after both fetches, by pomo_apply_project_names.
bool pomo_parse_task_page(const char *body, pomo_tasklist_t *out,
                          char proj_id[POMO_MAX_TASKS][POMO_ID_LEN],
                          char *next_cursor, size_t next_cap);

// Parse one page of GET /api/v1/projects. Appends to out, caps at POMO_MAX_PROJ.
bool pomo_parse_projects(const char *body, pomo_projmap_t *out,
                         char *next_cursor, size_t next_cap);

// Resolve proj_id[i] -> task[i].project through the map. An id with no match leaves the
// project name empty, which is what the faces already render for a project-less task.
void pomo_apply_project_names(pomo_tasklist_t *l,
                              const char proj_id[POMO_MAX_TASKS][POMO_ID_LEN],
                              const pomo_projmap_t *map);
