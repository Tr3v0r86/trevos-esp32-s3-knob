// pomodoist_parse.c — Todoist v1 JSON -> pomodoist core types. See pomodoist_parse.h.
//
// Host-testable on purpose: test_pomodoist_parse.c compiles this file against cJSON and
// pomodoist/core alone. That is why nothing here touches wifi, NVS, or the HTTP client —
// the transport hands over a body string and gets a tasklist back.
#include "pomodoist_parse.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "pomo_parse";

// Todoist reports planned effort as a duration, not a pomo count, so the mapping lives
// here in one place: round UP to whole 25-minute blocks (a 50-minute task is 2 pomos, a
// 30-minute task is 2 as well — you do not half-run a pomodoro), and clamp to the core's
// ceiling. A "day"-unit duration is a whole-day allocation, not focus time, so it plans
// nothing rather than 58 pomodoros.
static uint8_t pomos_from_duration(const cJSON *dur)
{
    const cJSON *amount = cJSON_GetObjectItem(dur, "amount");
    const cJSON *unit   = cJSON_GetObjectItem(dur, "unit");
    if (!cJSON_IsNumber(amount) || !cJSON_IsString(unit)) return 0;
    if (strcmp(unit->valuestring, "minute") != 0) return 0;
    int mins = amount->valueint;
    if (mins <= 0) return 0;
    int n = (mins + 24) / 25;
    return (uint8_t)(n > POMO_MAX_POMOS ? POMO_MAX_POMOS : n);
}

// "deep", "api" -> "#deep #api", stopping cleanly when the field runs out of room.
static void join_labels(const cJSON *labels, char *dst, size_t cap)
{
    size_t used = 0;
    int n = cJSON_GetArraySize(labels);
    for (int k = 0; k < n; k++) {
        const cJSON *lab = cJSON_GetArrayItem(labels, k);
        if (!cJSON_IsString(lab) || !lab->valuestring[0]) continue;
        char label[POMO_TAGS_LEN];
        pomo_task_text_copy(label, sizeof label, lab->valuestring);
        if (!label[0]) continue;
        int wrote = snprintf(dst + used, cap - used, "%s#%s", used ? " " : "", label);
        if (wrote <= 0 || (size_t)wrote >= cap - used) { dst[used] = '\0'; break; }  // out of room
        used += (size_t)wrote;
    }
}

// Shared prologue for both page parsers: parse, find "results", validate the cursor fits.
// Returns the root (caller deletes) or NULL, and hands back the results array + cursor node.
static cJSON *page_root(const char *body, const char *what, cJSON **results,
                        char *next_cursor, size_t next_cap)
{
    if (!body || !next_cursor || next_cap == 0) return NULL;
    next_cursor[0] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        const char *e = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "cJSON_Parse(%s) FAILED at offset %d; first 100 bytes: %.100s",
                 what, e ? (int)(e - body) : -1, body);
        return NULL;
    }
    cJSON *arr  = cJSON_GetObjectItem(root, "results");
    cJSON *next = cJSON_GetObjectItem(root, "next_cursor");
    // An oversized cursor is fatal rather than truncated: a truncated cursor silently
    // refetches page one forever.
    if (!cJSON_IsArray(arr) ||
        (cJSON_IsString(next) && strlen(next->valuestring) >= next_cap)) {
        ESP_LOGE(TAG, "%s response missing results or has an oversized cursor", what);
        cJSON_Delete(root);
        return NULL;
    }
    if (cJSON_IsString(next)) strcpy(next_cursor, next->valuestring);
    *results = arr;
    return root;
}

bool pomo_parse_task_page(const char *body, pomo_tasklist_t *out,
                          char proj_id[POMO_MAX_TASKS][POMO_ID_LEN],
                          char *next_cursor, size_t next_cap)
{
    if (!out) return false;
    cJSON *arr = NULL;
    cJSON *root = page_root(body, "tasks", &arr, next_cursor, next_cap);
    if (!root) return false;

    int n = cJSON_GetArraySize(arr);
    if (n > POMO_MAX_TASKS - out->count) n = POMO_MAX_TASKS - out->count;
    for (int i = 0; i < n; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        int slot = out->count;
        pomo_task_t *task = &out->task[slot];
        memset(task, 0, sizeof(*task));   // #34: every field starts rejected
        if (proj_id) proj_id[slot][0] = '\0';

        const cJSON *id = cJSON_GetObjectItem(it, "id");
        if (cJSON_IsString(id)) strncpy(task->id, id->valuestring, POMO_ID_LEN - 1);

        const cJSON *content = cJSON_GetObjectItem(it, "content");
        if (cJSON_IsString(content))
            pomo_task_text_copy(task->title, sizeof task->title, content->valuestring);

        const cJSON *desc = cJSON_GetObjectItem(it, "description");
        if (cJSON_IsString(desc) && desc->valuestring[0])
            pomo_task_text_copy(task->desc, sizeof task->desc, desc->valuestring);

        const cJSON *labels = cJSON_GetObjectItem(it, "labels");
        if (cJSON_IsArray(labels)) join_labels(labels, task->tags, sizeof task->tags);

        // Todoist's priority is 1..4 with 4 meaning p1. Anything else (a string, a 0, a 9)
        // is rejected to zero rather than clamped: an invented priority ranks a task wrong
        // on the glass, and 0 reads as "unknown" everywhere downstream.
        const cJSON *prio = cJSON_GetObjectItem(it, "priority");
        if (cJSON_IsNumber(prio) && prio->valueint >= 1 && prio->valueint <= 4)
            task->priority = (uint8_t)prio->valueint;

        const cJSON *dur = cJSON_GetObjectItem(it, "duration");
        if (cJSON_IsObject(dur)) task->pomos = pomos_from_duration(dur);

        const cJSON *pid = cJSON_GetObjectItem(it, "project_id");
        if (proj_id && cJSON_IsString(pid))
            strncpy(proj_id[slot], pid->valuestring, POMO_ID_LEN - 1);

        out->count++;
    }
    cJSON_Delete(root);
    return true;
}

bool pomo_parse_projects(const char *body, pomo_projmap_t *out,
                         char *next_cursor, size_t next_cap)
{
    if (!out) return false;
    cJSON *arr = NULL;
    cJSON *root = page_root(body, "projects", &arr, next_cursor, next_cap);
    if (!root) return false;

    int n = cJSON_GetArraySize(arr);
    if (n > POMO_MAX_PROJ - out->count) n = POMO_MAX_PROJ - out->count;
    for (int i = 0; i < n; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        const cJSON *id = cJSON_GetObjectItem(it, "id");
        const cJSON *name = cJSON_GetObjectItem(it, "name");
        if (!cJSON_IsString(id) || !cJSON_IsString(name)) continue;   // an entry we cannot key is useless
        pomo_proj_t *p = &out->p[out->count];
        memset(p, 0, sizeof(*p));
        strncpy(p->id, id->valuestring, POMO_ID_LEN - 1);
        pomo_task_text_copy(p->name, sizeof p->name, name->valuestring);
        out->count++;
    }
    cJSON_Delete(root);
    return true;
}

void pomo_apply_project_names(pomo_tasklist_t *l,
                              const char proj_id[POMO_MAX_TASKS][POMO_ID_LEN],
                              const pomo_projmap_t *map)
{
    if (!l || !proj_id || !map) return;
    // ponytail: linear scan, 32 tasks x 48 projects worst case, once per hourly fetch.
    // Sort or hash it only if the project list ever outgrows a phone screen.
    for (int i = 0; i < l->count; i++) {
        l->task[i].project[0] = '\0';
        if (!proj_id[i][0]) continue;
        for (int k = 0; k < map->count; k++) {
            if (strcmp(proj_id[i], map->p[k].id) != 0) continue;
            memcpy(l->task[i].project, map->p[k].name, sizeof l->task[i].project);
            break;
        }
    }
}
