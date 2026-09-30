// Host test for trev_bar2.h: cc -std=c99 -Wall -Wextra -Werror -Ios/trevos/include os/trevos/test_trev_bar2.c -o /tmp/tb && /tmp/tb
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "trev_bar2.h"

// The outer cap of a pill: its centre is half a height in from the outer edge, its reach is that radius.
static double cap_reach(int x_outer_centre, int dh)
{
    double dx = x_outer_centre - 180.0, dy = tt_bar2_top(dh) + TT_BAR2_H / 2.0 - 180.0;
    return sqrt(dx * dx + dy * dy) + TT_BAR2_H / 2.0;
}

int main(void)
{
    assert(tt_bar2_top(360) == 268);
    assert(tt_bar2_x(360, -1, false) == 76);
    assert(tt_bar2_x(360, 0, false) == 188);
    assert(tt_bar2_x(360, 0, true) == 132);
    assert(tt_bar2_zone(179, 360, false) == -1);
    assert(tt_bar2_zone(180, 360, false) == 0);
    assert(tt_bar2_zone(20, 360, true) == 0);
    assert(tt_bar2_zone(340, 360, true) == 0);

    double left = cap_reach(tt_bar2_x(360, -1, false) + TT_BAR2_H / 2, 360);
    double right = cap_reach(tt_bar2_x(360, 0, false) + TT_BAR2_W - TT_BAR2_H / 2, 360);
    assert(fabs(left - 159.2) < 0.1);
    assert(left <= 168.0 && right <= 168.0);
    assert(cap_reach(tt_bar2_x(360, 0, true) + TT_BAR2_H / 2, 360) <= 168.0);
    puts("PASS trev_bar2");
    return 0;
}
