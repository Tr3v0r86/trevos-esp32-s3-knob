/* Host test for the ring geometry (D5, D6, D13). No LVGL.
 *   cc -std=c99 -Wall -Wextra -Werror -Ios/trevos/include os/trevos/test_trev_ring.c \
 *      os/trevos/ui/trev_ring_geom.c -lm -o /tmp/tr && /tmp/tr */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "trev_ring.h"

/* A point at radius r, `deg` clockwise from 12 o'clock, rounded to a pixel. */
static int hit_at(double deg, double r)
{
    double a = deg * 3.14159265358979323846 / 180.0;
    return tt_ring_hit(TT_RING.cx + (int)lround(r * sin(a)), TT_RING.cy - (int)lround(r * cos(a)));
}

int main(void)
{
    assert(TT_RING.cx == 180 && TT_RING.cy == 180 && TT_RING.r_disc == 118);
    assert(TT_RING.r_in == 126 && TT_RING.r_out == 172 && TT_RING.r_hit == 180);

    /* every segment centre at r=149 hits itself */
    for (int s = 0; s < TT_RING_SEGS; s++) {
        int x, y;
        tt_ring_seg_centre(s, 149, &x, &y);
        assert(tt_ring_hit(x, y) == s);
    }
    { int x, y; tt_ring_seg_centre(4, 149, &x, &y); assert(x == 180 && y == 329); }

    /* segment borders sit at 22.5 degrees; pixels picked at r~149 for atan2 = 22.44 / 22.59 */
    assert(tt_ring_hit(237, 42) == 0);              /* 22.44 degrees */
    assert(tt_ring_hit(237, 43) == 1);              /* 22.59 degrees */
    assert(tt_ring_hit(123, 42) == 0);              /* -22.44 */
    assert(tt_ring_hit(123, 43) == 7);              /* -22.59 */

    /* radial bands: disc, snap zone (mid 122), band, outside */
    assert(tt_ring_hit(180, 180) == TT_RING_DISC);
    assert(hit_at(0, 0) == TT_RING_DISC);
    assert(hit_at(90, 117) == TT_RING_DISC);
    assert(hit_at(90, 121) == TT_RING_DISC);
    assert(hit_at(90, 123) == 2);
    assert(hit_at(90, 180) == 2);
    assert(hit_at(90, 181) == -1);

    /* wheel stepping: wraps both ways, skips empties */
    uint8_t m = 0x13;                               /* segments 0, 1, 4 */
    assert(tt_ring_next(0, 1, m) == 1);
    assert(tt_ring_next(1, 1, m) == 4);
    assert(tt_ring_next(4, 1, m) == 0);             /* wrap forward */
    assert(tt_ring_next(0, -1, m) == 4);            /* wrap back */
    assert(tt_ring_next(4, -1, m) == 1);
    assert(tt_ring_next(1, -1, m) == 0);
    assert(tt_ring_next(0, 1, 0) == -1);
    assert(tt_ring_next(3, 1, 0x08) == 3);          /* a lone segment stays put */

    /* slots: settings pinned to 4, the rest clockwise skipping it */
    bool a[3] = { false, false, true };
    int8_t seg[9];
    assert(tt_ring_slots(a, 3, seg) == 0x13);
    assert(seg[0] == 0 && seg[1] == 1 && seg[2] == 4);
    bool b[8] = { false, false, false, false, false, false, false, true };
    assert(tt_ring_slots(b, 8, seg) == 0xFF);
    assert(seg[7] == 4 && seg[3] == 3 && seg[4] == 5 && seg[6] == 7);
    bool nine[9] = { false };
    assert(tt_ring_slots(nine, 9, seg) == -1);
    assert(tt_ring_slots(nine, 8, seg) == -1);      /* 8 non-settings apps: only 7 slots */

    puts("PASS trev_ring");
    return 0;
}
