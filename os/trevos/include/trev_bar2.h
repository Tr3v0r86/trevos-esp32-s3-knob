// trev_bar2.h: pure, no LVGL. The two-pill bottom chord from the approved boards.
// A face that draws two verbs (or one) instead of the disk's thirds uses these numbers for
// both the drawing and the hit test, so a pill and its firing half cannot drift apart.
#pragma once
#include <stdbool.h>

#define TT_BAR2_W    96
#define TT_BAR2_H    44
#define TT_BAR2_GAP  16
#define TT_BAR2_DROP 110   /* pill centre below the display centre: y 268..312 on 360 */

// Top edge of the pills (and of the bar zone) for a display dh high: 268 on 360.
static inline int tt_bar2_top(int dh) { return dh / 2 + TT_BAR2_DROP - TT_BAR2_H / 2; }

// Left edge of a pill. zone -1 = left pill, 0 = right pill; lone = a single centred pill
// (always zone 0). On 360: -1 -> 76, 0 -> 188, lone -> 132.
static inline int tt_bar2_x(int dw, int zone, bool lone)
{
    if (lone) return (dw - TT_BAR2_W) / 2;
    int x0 = (dw - (2 * TT_BAR2_W + TT_BAR2_GAP)) / 2;
    return zone < 0 ? x0 : x0 + TT_BAR2_W + TT_BAR2_GAP;
}

// Which verb a press at display x fires: -1 left half, 0 right half. A lone pill owns the
// whole width: always 0.
static inline int tt_bar2_zone(int x, int dw, bool lone) { return lone || x >= dw / 2 ? 0 : -1; }
