// cal_glance.c - the glance rules. See cal_glance.h; each rule has a case in test_cal_glance.c.
#include "cal_glance.h"
#include <stdio.h>
#include <string.h>

static int start_min(const cal_event_t *e) { return e->sh * 60 + e->sm; }
static int end_min(const cal_event_t *e)   { return e->eh * 60 + e->em; }

static int first_timed(const cal_day_t *d)
{
    return d ? cal_day_next_at(d, 0) : -1;
}

void cal_glance(const cal_day_t *today, const cal_day_t *tomorrow, int now_min, bool clock_ok,
                bool have_data, long age_s, cal_glance_t *g)
{
    memset(g, 0, sizeof *g);
    g->now_ev = g->next_ev = g->mins = -1;
    g->stale = age_s < 0 || age_s > CAL_STALE_S;
    if (!clock_ok)  { g->state = CAL_G_CLOCK_UNSET; return; }
    if (!have_data) { g->state = CAL_G_WAITING; return; }
    if (!today)     { g->state = CAL_G_NO_TODAY; return; }

    g->more = today->more;
    bool any_timed = false;
    for (int i = 0; i < today->n; i++) {
        const cal_event_t *e = &today->ev[i];
        if (e->allday) { g->allday++; continue; }
        any_timed = true;
        if (start_min(e) <= now_min && now_min < end_min(e)) {
            if (g->now_ev < 0) g->now_ev = i;
            else {
                g->now_extra++;
                if (end_min(e) < end_min(&today->ev[g->now_ev])) g->now_ev = i;
            }
        }
    }

    // Next starts strictly after now; an event starting this minute is "now", not "next".
    g->next_ev = cal_day_next_at(today, now_min + 1);

    if (g->now_ev >= 0) {
        g->state = CAL_G_BUSY;
        g->mins = end_min(&today->ev[g->now_ev]) - now_min;
    } else if (g->next_ev >= 0) {
        g->state = CAL_G_FREE;
        g->mins = start_min(&today->ev[g->next_ev]) - now_min;
    } else {
        g->state = any_timed ? CAL_G_DONE : CAL_G_EMPTY;
    }

    // A truncated day may hide what is next, so it never claims the rest of the day is free.
    if (g->more && (g->state == CAL_G_DONE || g->state == CAL_G_EMPTY)) g->state = CAL_G_UNSURE;

    if (g->next_ev < 0 && g->state != CAL_G_UNSURE) {
        int t = first_timed(tomorrow);
        if (t >= 0) { g->next_ev = t; g->next_tomorrow = true; }
    }
}

void cal_glance_dur(int mins, char out[16])
{
    if (mins < 1)       snprintf(out, 16, "< 1 min");
    else if (mins < 60) snprintf(out, 16, "%d min", mins);
    else if (mins % 60) snprintf(out, 16, "%d h %d", mins / 60, mins % 60);
    else                snprintf(out, 16, "%d h", mins / 60);
}

int cal_glance_list(const cal_day_t *d, int now_min, int8_t idx[CAL_MAX_EVENTS], int *rest)
{
    int n = 0;
    *rest = -1;
    if (!d) return 0;
    for (int i = 0; i < d->n && n < CAL_MAX_EVENTS; i++)
        if (!d->ev[i].allday) idx[n++] = (int8_t)i;
    if (!n) return 0;
    int next = cal_day_next_at(d, now_min + 1);   // strictly after now: starting this minute is "now"
    int on = cal_day_current_at(d, now_min);
    int want = next >= 0 ? next : on;
    *rest = n - 1;
    for (int p = 0; want >= 0 && p < n; p++)
        if (idx[p] == want) { *rest = p; break; }
    return n;
}

void cal_glance_label(const cal_day_t *d, int ev, int now_min, char out[32])
{
    const cal_event_t *e = &d->ev[ev];
    char b[16];
    if (start_min(e) <= now_min && now_min < end_min(e)) {
        cal_glance_dur(end_min(e) - now_min, b);
        snprintf(out, 32, "NOW \xC2\xB7 %s LEFT", b);
    } else if (end_min(e) <= now_min) {
        snprintf(out, 32, "ENDED");
        return;
    } else {
        cal_glance_dur(start_min(e) - now_min, b);
        snprintf(out, 32, ev == cal_day_next_at(d, now_min + 1) ? "NEXT \xC2\xB7 IN %s" : "IN %s", b);
    }
    for (char *p = out; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
}
