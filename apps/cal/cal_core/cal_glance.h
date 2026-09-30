// cal_glance.h - what a glance face says right now: on now or free, what is next, how long.
//
// Pure C99, host-tested (test_cal_glance.c). Every face that answers "now / next / how long"
// renders a cal_glance_t, so the placeholder and the designed face share one set of rules and
// differ only in layout (ADR-0017 point 6). Times are local wall minutes; the caller converts.
#pragma once
#include <stdbool.h>
#include "cal_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CAL_STALE_S
#define CAL_STALE_S (15 * 60)   // three missed 5-minute refreshes
#endif

typedef enum {
    CAL_G_CLOCK_UNSET,   // no wall time yet: say nothing about now or next
    CAL_G_WAITING,       // clock set, nothing ever received
    CAL_G_NO_TODAY,      // data exists but not for today: never read as free
    CAL_G_BUSY,          // a timed event is on now
    CAL_G_FREE,          // between events, another starts later today
    CAL_G_DONE,          // today had timed events and none are left
    CAL_G_EMPTY,         // today has no timed events at all
    CAL_G_UNSURE,        // nothing visible is left, but the day was truncated: say so, not "free"
} cal_glance_state_t;

typedef struct {
    cal_glance_state_t state;
    int  now_ev;          // index into today, BUSY only (earliest-ending current event), else -1
    int  now_extra;       // other events also on now
    int  next_ev;         // index into today, or into tomorrow when next_tomorrow; -1 = none known
    bool next_tomorrow;
    int  mins;            // BUSY: minutes until now_ev ends; FREE: until next_ev starts; else -1
    int  allday;          // all-day events today, shown as a chip, never as "now"
    int  more;            // today's events dropped past the cap: a truncated day is never "free"
    bool stale;           // last good sync older than CAL_STALE_S
} cal_glance_t;

// today/tomorrow may be NULL (absent from the window). now_min is minutes since local
// midnight. age_s is seconds since the last good sync, or < 0 if never.
void cal_glance(const cal_day_t *today, const cal_day_t *tomorrow, int now_min, bool clock_ok,
                bool have_data, long age_s, cal_glance_t *out);

// "25 min", "< 1 min", "1 h 20". out needs 16 bytes.
void cal_glance_dur(int mins, char out[16]);

// Today's timed events in display order (the caller sorted the day), and where the wheel rests:
// the next start after now, else the event on now, else the last; -1 with no timed events.
// idx[] holds indexes into d->ev; returns how many. d may be NULL.
int  cal_glance_list(const cal_day_t *d, int now_min, int8_t idx[CAL_MAX_EVENTS], int *rest);

// "NOW \xC2\xB7 10 MIN LEFT", "NEXT \xC2\xB7 IN 10 MIN", "IN 2 H 40", "ENDED" (uppercased cal_glance_dur).
// NEXT is the event cal_glance_list() rests on ahead of now: the first start after now.
void cal_glance_label(const cal_day_t *d, int ev, int now_min, char out[32]);

#ifdef __cplusplus
}
#endif
