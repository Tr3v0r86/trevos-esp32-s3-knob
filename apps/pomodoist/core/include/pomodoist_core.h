// pomodoist_core.h — board- and UI-agnostic pomodoro engine.
//
// Pure logic: a task list, a countdown timer, and focus/pause/done state. No LVGL,
// no display, no input hardware, no network. A face (the design agent's job) owns
// presentation: it reads this state and drives it through the ops below. Seeded from
// the puck's app_pomotodo.c, generalised away from the puck's LVGL.
//
// Threading: the core is not internally locked. Call its ops from one context (on the
// T3, that is the LVGL task: input handlers and the tick both run there).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POMO_MAX_TASKS       32
#define POMO_ID_LEN          32
#define POMO_TITLE_LEN       80
#define POMO_MAX_POMOS       8
#define POMO_PROJ_LEN        20
#define POMO_TAGS_LEN        48
// Board-capped, and the only field that is. 120 chars is all a rectangular face can show, and
// a tasklist is 32 of these structs, so the cap is real memory (each +1 costs 32 bytes). The
// disk raises it to 480 in its top-level CMakeLists because D18's detail card scrolls, so a
// description that overflows the card is readable rather than lost. Every consumer sizes its
// buffers from this macro or from sizeof, never from a literal - keep it that way.
#ifndef POMO_DESC_LEN
#define POMO_DESC_LEN       120
#endif
#define POMO_MIN_MINUTES      5
#define POMO_MAX_MINUTES     60
#define POMO_DEFAULT_MINUTES 25

typedef struct {
    char    id[POMO_ID_LEN];        // stable Todoist task id (write-back anchor); empty = no id (serial push)
    char    title[POMO_TITLE_LEN];
    char    project[POMO_PROJ_LEN];
    char    tags[POMO_TAGS_LEN];    // space-joined "#deep-work #api" (Todoist labels); may be empty
    char    desc[POMO_DESC_LEN];    // one-line description (Todoist description); may be empty
    uint8_t pomos;   // planned pomodoros, 0..8
    uint8_t priority;   // 1..4 Todoist scale, 1 = normal, 4 = p1
    bool    done;
} pomo_task_t;

typedef struct {
    pomo_task_t task[POMO_MAX_TASKS];
    int         count;
} pomo_tasklist_t;

typedef struct {
    pomo_tasklist_t tasks;
    int      active;     // active task index
    int      cursor;     // list cursor for a TASKS view
    uint32_t total_s;    // configured focus length
    uint32_t left_s;     // remaining
    bool     running;
    bool     dirty;      // any state change sets this; the face renders then clears it
    uint32_t _prev_ms;   // tick bookkeeping (internal; 0 = re-anchor on next tick)
    uint32_t _acc_ms;
} pomo_core_t;

// lifecycle
void pomo_core_init(pomo_core_t *c);                                   // 25:00, empty list
void pomo_core_set_tasks(pomo_core_t *c, const pomo_tasklist_t *list); // from todoist-sync

// timer
void pomo_core_nudge_minutes(pomo_core_t *c, int delta_min);  // idle only; clamps 5..60
void pomo_core_toggle(pomo_core_t *c);                        // start/pause; restart if finished
void pomo_core_reset(pomo_core_t *c);                         // explicit reset; keeps selected task
void pomo_core_tick(pomo_core_t *c, uint32_t now_ms);         // advance the countdown
void pomo_core_complete_active(pomo_core_t *c);               // DONE: drop active task, advance, reset

// task selection (for a list view)
void pomo_core_cursor_move(pomo_core_t *c, int delta);
void pomo_core_select(pomo_core_t *c, int index);
size_t pomo_task_text_copy(char *dst, size_t cap, const char *src);
int pomo_tasklist_append(pomo_tasklist_t *dst, const pomo_tasklist_t *page);

// day arithmetic helpers
int pomo_core_pomos_left(const pomo_core_t *c, const uint8_t *done_today);  // pomos still owed across undone tasks. done_today[i] pairs with c->tasks.task[i].
uint32_t pomo_core_eta_s(int left, uint32_t focus_s, uint32_t short_break_s, uint32_t long_break_s);  // seconds from now until left pomos finish, breaks included. long break every 4th.

// read helper for the face
const pomo_task_t *pomo_core_active_task(const pomo_core_t *c);  // NULL if list empty
