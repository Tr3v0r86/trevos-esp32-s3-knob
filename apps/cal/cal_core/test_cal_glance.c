// test_cal_glance.c - one case per glance rule. Run via `make test`.
#include "cal_glance.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ev(cal_day_t *d, bool allday, int sh, int sm, int eh, int em, const char *t)
{
    cal_event_t *e = &d->ev[d->n++];
    memset(e, 0, sizeof *e);
    e->allday = allday; e->sh = sh; e->sm = sm; e->eh = eh; e->em = em;
    snprintf(e->title, sizeof e->title, "%s", t);
}
#define M(h, m) ((h) * 60 + (m))

int main(void)
{
    cal_day_t d, tm; cal_glance_t g; char b[16];
    memset(&d, 0, sizeof d); memset(&tm, 0, sizeof tm);
    ev(&d, true, 0, 0, 0, 0, "Reports due");
    ev(&d, false, 9, 0, 10, 0, "Ops");
    ev(&d, false, 9, 30, 9, 45, "Call");          // overlaps Ops, ends first
    ev(&d, false, 13, 0, 14, 0, "SLT");
    ev(&d, false, 23, 30, 24, 0, "Late");
    ev(&tm, false, 8, 0, 9, 0, "Breakfast");

    cal_glance(&d, &tm, M(9, 0), false, true, 10, &g);
    assert(g.state == CAL_G_CLOCK_UNSET && g.now_ev < 0 && g.next_ev < 0);           // clock unset
    cal_glance(&d, &tm, M(9, 0), true, false, -1, &g);
    assert(g.state == CAL_G_WAITING && g.stale);                                      // never synced
    cal_glance(NULL, &tm, M(9, 0), true, true, 10, &g);
    assert(g.state == CAL_G_NO_TODAY);                                                // no today

    cal_glance(&d, &tm, M(8, 0), true, true, 10, &g);                                 // free between
    assert(g.state == CAL_G_FREE && g.next_ev == 1 && g.mins == 60 && g.allday == 1 && !g.stale);
    cal_glance(&d, &tm, M(9, 0), true, true, 10, &g);                                 // starts this minute = now
    assert(g.state == CAL_G_BUSY && g.now_ev == 1 && g.mins == 60 && g.next_ev == 2);
    cal_glance(&d, &tm, M(9, 35), true, true, 10, &g);                                // overlap: earliest-ending
    assert(g.state == CAL_G_BUSY && g.now_ev == 2 && g.now_extra == 1 && g.mins == 10 && g.next_ev == 3);
    cal_glance(&d, &tm, M(9, 59), true, true, 10, &g);                                // under a minute left
    assert(g.state == CAL_G_BUSY && g.mins == 1);
    cal_glance(&d, &tm, M(23, 45), true, true, 10, &g);                               // ends at 24:00, next is tomorrow
    assert(g.state == CAL_G_BUSY && g.mins == 15 && g.next_tomorrow && g.next_ev == 0);
    cal_glance(&d, NULL, M(23, 59) + 1, true, true, 10, &g);                          // after last, tomorrow unknown
    assert(g.state == CAL_G_DONE && g.next_ev < 0);

    cal_day_t a; memset(&a, 0, sizeof a); ev(&a, true, 0, 0, 0, 0, "Holiday");
    cal_glance(&a, &tm, M(10, 0), true, true, 10, &g);                                // all-day only: empty, not busy
    assert(g.state == CAL_G_EMPTY && g.allday == 1 && g.next_tomorrow);

    cal_day_t t; memset(&t, 0, sizeof t); ev(&t, false, 8, 0, 9, 0, "Early"); t.more = 3;
    cal_glance(&t, &tm, M(12, 0), true, true, 10, &g);                                // truncated day never free
    assert(g.state == CAL_G_UNSURE && g.more == 3 && g.next_ev < 0);

    cal_glance(&d, &tm, M(8, 0), true, true, CAL_STALE_S, &g);    assert(!g.stale);  // stale boundary
    cal_glance(&d, &tm, M(8, 0), true, true, CAL_STALE_S + 1, &g); assert(g.stale);

    cal_glance_dur(0, b);  assert(!strcmp(b, "< 1 min"));
    cal_glance_dur(59, b); assert(!strcmp(b, "59 min"));
    cal_glance_dur(60, b); assert(!strcmp(b, "1 h"));
    cal_glance_dur(80, b); assert(!strcmp(b, "1 h 20"));
    // The glance list and labels (the puck's Cal face): a day shaped like the sim fixture.
    cal_day_t f; memset(&f, 0, sizeof f);
    ev(&f, true, 0, 0, 0, 0, "A day for making");
    static const int fx[9][4] = {{7,30,8,0},{8,45,10,45},{9,0,9,30},{9,30,10,0},{10,0,10,30},
                                 {12,0,13,0},{14,0,15,0},{16,30,17,0},{18,0,19,30}};
    for (int i = 0; i < 9; i++) ev(&f, false, fx[i][0], fx[i][1], fx[i][2], fx[i][3], "x");
    int8_t ix[CAL_MAX_EVENTS]; int rest; char lb[32];
    assert(cal_glance_list(&f, 560, ix, &rest) == 9 && rest == 3 && ix[0] == 1 && ix[8] == 9);   // "4 / 9"
    cal_glance_label(&f, ix[3], 560, lb); assert(!strcmp(lb, "NEXT \xC2\xB7 IN 10 MIN"));
    cal_glance_label(&f, ix[2], 560, lb); assert(!strcmp(lb, "NOW \xC2\xB7 10 MIN LEFT"));
    cal_glance_label(&f, ix[5], 560, lb); assert(!strcmp(lb, "IN 2 H 40"));
    cal_glance_label(&f, ix[0], 560, lb); assert(!strcmp(lb, "ENDED"));
    assert(cal_glance_list(&f, 1200, ix, &rest) == 9 && rest == 8);                              // all over: the last
    assert(cal_glance_list(&f, 0, ix, &rest) == 9 && rest == 0);
    cal_glance_label(&f, ix[0], 0, lb); assert(!strcmp(lb, "NEXT \xC2\xB7 IN 7 H 30"));
    assert(cal_glance_list(&f, M(10, 5), ix, &rest) == 9 && rest == 5);                          // the 10:00 one is on now, 12:00 is next
    assert(cal_glance_list(&a, 560, ix, &rest) == 0 && rest == -1);                              // all-day only
    cal_day_t z; memset(&z, 0, sizeof z);
    assert(cal_glance_list(&z, 560, ix, &rest) == 0 && rest == -1);                              // empty
    assert(cal_glance_list(NULL, 560, ix, &rest) == 0 && rest == -1);
    {   // On now with nothing after it: the on-now event rests, not the last by index.
        cal_day_t o; memset(&o, 0, sizeof o); ev(&o, false, 9, 0, 12, 0, "Long"); ev(&o, false, 9, 30, 10, 0, "Short");
        assert(cal_glance_list(&o, M(9, 45), ix, &rest) == 2 && rest == 0);                      // Long (pos 0) is on now, Short too; first on-now
    }
    {   // An event starting this minute is NOW; the following one is the NEXT.
        assert(cal_glance_list(&f, M(9, 0), ix, &rest) == 9 && rest == 3);
        cal_glance_label(&f, ix[2], M(9, 0), lb); assert(!strcmp(lb, "NOW \xC2\xB7 30 MIN LEFT"));
        cal_glance_label(&f, ix[3], M(9, 0), lb); assert(!strcmp(lb, "NEXT \xC2\xB7 IN 30 MIN"));
    }
    // The parser counts what it drops past the cap, which is what makes UNSURE reachable.
    static char js[8192]; int n = snprintf(js, sizeof js, "{\"days_list\":[{\"date\":\"2026-09-29\",\"events\":[");
    for (int i = 0; i < CAL_MAX_EVENTS + 2; i++)
        n += snprintf(js + n, sizeof js - n, "%s{\"title\":\"e%d\",\"start\":\"08:00\",\"end\":\"08:30\"}", i ? "," : "", i);
    snprintf(js + n, sizeof js - n, "]}]}");
    cal_window_t *w = malloc(sizeof *w);
    assert(cal_model_parse(js, strlen(js), w) && w->day[0].n == CAL_MAX_EVENTS && w->day[0].more == 2);
    free(w);
    puts("cal_glance: all cases pass");
    return 0;
}
