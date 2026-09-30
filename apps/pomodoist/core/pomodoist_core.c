// pomodoist_core.c — the pomodoro engine. See pomodoist_core.h.
#include "pomodoist_core.h"
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int next_open(const pomo_core_t *c, int from)
{
    if (c->tasks.count == 0) return 0;
    for (int i = 1; i <= c->tasks.count; i++) {
        int idx = (from + i) % c->tasks.count;
        if (!c->tasks.task[idx].done) return idx;
    }
    return from < 0 ? 0 : from;   // all done: stay
}

void pomo_core_init(pomo_core_t *c)
{
    memset(c, 0, sizeof(*c));
    c->total_s = POMO_DEFAULT_MINUTES * 60;
    c->left_s  = c->total_s;
    c->dirty   = true;
}

void pomo_core_set_tasks(pomo_core_t *c, const pomo_tasklist_t *list)
{
    c->tasks = *list;
    if (c->tasks.count > POMO_MAX_TASKS) c->tasks.count = POMO_MAX_TASKS;
    c->active = next_open(c, -1);
    c->cursor = c->active;
    c->dirty  = true;
}

void pomo_core_nudge_minutes(pomo_core_t *c, int delta_min)
{
    if (c->running || c->left_s != c->total_s) return;   // idle only
    int m = clampi((int)(c->total_s / 60) + delta_min, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
    c->total_s = (uint32_t)m * 60;
    c->left_s  = c->total_s;
    c->dirty   = true;
}

void pomo_core_toggle(pomo_core_t *c)
{
    if (!c->running && c->left_s == 0) c->left_s = c->total_s;   // restart a finished block
    c->running = !c->running;
    if (c->running) { c->_prev_ms = 0; c->_acc_ms = 0; }         // re-anchor on next tick
    c->dirty = true;
}

void pomo_core_reset(pomo_core_t *c)
{
    c->running = false;
    c->left_s = c->total_s;
    c->_prev_ms = 0;
    c->_acc_ms = 0;
    c->dirty = true;
}

void pomo_core_tick(pomo_core_t *c, uint32_t now_ms)
{
    if (!c->running) return;
    if (c->_prev_ms == 0) { c->_prev_ms = now_ms; return; }
    c->_acc_ms += now_ms - c->_prev_ms;
    c->_prev_ms = now_ms;
    while (c->_acc_ms >= 1000 && c->left_s > 0) {
        c->_acc_ms -= 1000;
        c->left_s--;
        c->dirty = true;
    }
    if (c->left_s == 0) { c->running = false; c->dirty = true; }
}

void pomo_core_complete_active(pomo_core_t *c)
{
    if (c->tasks.count > 0) {
        for (int k = c->active; k < c->tasks.count - 1; k++) c->tasks.task[k] = c->tasks.task[k + 1];
        c->tasks.count--;
        if (c->active >= c->tasks.count) c->active = c->tasks.count > 0 ? c->tasks.count - 1 : 0;
        c->cursor = c->active;
    }
    c->running = false;
    c->left_s  = c->total_s;
    c->dirty   = true;
}

void pomo_core_cursor_move(pomo_core_t *c, int delta)
{
    if (c->tasks.count == 0) return;
    int cursor = c->cursor % c->tasks.count;
    if (cursor < 0) cursor += c->tasks.count;
    delta %= c->tasks.count;
    cursor += delta;
    if (cursor < 0) cursor += c->tasks.count;
    else if (cursor >= c->tasks.count) cursor -= c->tasks.count;
    c->cursor = cursor;
    c->dirty  = true;
}

void pomo_core_select(pomo_core_t *c, int index)
{
    if (index < 0 || index >= c->tasks.count) return;
    c->active = index;
    c->cursor = index;
    c->dirty  = true;
}

size_t pomo_task_text_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    size_t written = 0;
    if (!src) { dst[0] = '\0'; return 0; }

    while (*src) {
        const unsigned char *p = (const unsigned char *)src;
        const char *replacement;
        char ascii[2] = { 0, '\0' };
        size_t consumed = 1;

        if (p[0] < 0x80) {
            ascii[0] = (char)p[0];
            replacement = ascii;
        } else if (p[0] == 0xC2 && p[1] == 0xA0) {
            replacement = " ";
            consumed = 2;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x93 || p[2] == 0x94)) {
            replacement = "-";
            consumed = 3;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x98 || p[2] == 0x99)) {
            replacement = "'";
            consumed = 3;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x9C || p[2] == 0x9D)) {
            replacement = "\"";
            consumed = 3;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {
            replacement = "...";
            consumed = 3;
        } else {
            if (p[0] >= 0xF0 && p[0] <= 0xF4) consumed = 4;
            else if (p[0] >= 0xE0 && p[0] <= 0xEF) consumed = 3;
            else if (p[0] >= 0xC2 && p[0] <= 0xDF) consumed = 2;
            src++;
            while (--consumed && ((unsigned char)*src & 0xC0) == 0x80) src++;
            continue;
        }

        size_t len = strlen(replacement);
        if (written + len >= cap) break;
        memcpy(dst + written, replacement, len);
        written += len;
        src += consumed;
    }
    dst[written] = '\0';
    return written;
}

int pomo_tasklist_append(pomo_tasklist_t *dst, const pomo_tasklist_t *page)
{
    if (!dst || !page || dst->count < 0 || dst->count >= POMO_MAX_TASKS || page->count <= 0) return 0;
    int count = page->count;
    if (count > POMO_MAX_TASKS) count = POMO_MAX_TASKS;
    if (count > POMO_MAX_TASKS - dst->count) count = POMO_MAX_TASKS - dst->count;
    memcpy(&dst->task[dst->count], page->task, (size_t)count * sizeof(page->task[0]));
    dst->count += count;
    return count;
}

int pomo_core_pomos_left(const pomo_core_t *c, const uint8_t *done_today)
{
    int left = 0;
    for (int i = 0; i < c->tasks.count; i++) {
        const pomo_task_t *t = &c->tasks.task[i];
        if (t->done || t->pomos == 0) continue;
        int d = done_today ? done_today[i] : 0;
        if (d < t->pomos) left += t->pomos - d;
    }
    return left;
}

uint32_t pomo_core_eta_s(int left, uint32_t focus_s, uint32_t short_break_s, uint32_t long_break_s)
{
    if (left <= 0) return 0;
    uint32_t s = 0;
    for (int i = 1; i <= left; i++) {
        s += focus_s;
        if (i == left) break;                       // no break after the last block
        s += (i % 4 == 0) ? long_break_s : short_break_s;
    }
    return s;
}

const pomo_task_t *pomo_core_active_task(const pomo_core_t *c)
{
    return c->tasks.count > 0 ? &c->tasks.task[c->active] : NULL;
}
