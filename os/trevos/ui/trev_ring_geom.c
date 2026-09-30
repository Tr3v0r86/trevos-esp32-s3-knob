// trev_ring_geom.c - ring home geometry, no LVGL (see trev_ring.h).
#include "trev_ring.h"
#include <math.h>

const tt_ring_geom_t TT_RING = { 180, 180, 118, 126, 172, 180 };

#define TT_PI 3.14159265358979323846

int tt_ring_hit(int x, int y)
{
    int dx = x - TT_RING.cx, dy = y - TT_RING.cy;
    double r = sqrt((double)(dx * dx + dy * dy));
    if (r > TT_RING.r_hit) return -1;
    // The gap between disc and band (r_disc..r_in) snaps to whichever edge is nearer (D13).
    if (r < (TT_RING.r_disc + TT_RING.r_in) / 2.0) return TT_RING_DISC;
    double deg = atan2((double)dx, (double)-dy) * 180.0 / TT_PI;   // clockwise from 12
    if (deg < 0) deg += 360.0;
    // Segments tile the circle here; the 2 degree drawn gaps are not dead zones.
    return (int)((deg + 22.5) / 45.0) % TT_RING_SEGS;
}

int tt_ring_next(int sel, int dir, uint8_t occupied)
{
    if (!occupied) return -1;
    if (sel < 0 || sel >= TT_RING_SEGS) sel = dir > 0 ? TT_RING_SEGS - 1 : 0;
    for (int i = 1; i <= TT_RING_SEGS; i++) {
        int s = ((sel + i * dir) % TT_RING_SEGS + TT_RING_SEGS) % TT_RING_SEGS;
        if (occupied & (1u << s)) return s;
    }
    return -1;
}

void tt_ring_seg_centre(int seg, int r, int *x, int *y)
{
    double a = seg * 45.0 * TT_PI / 180.0;
    *x = TT_RING.cx + (int)lround(r * sin(a));
    *y = TT_RING.cy - (int)lround(r * cos(a));
}

int tt_ring_slots(const bool *is_settings, int n, int8_t seg_of[])
{
    if (n > TT_RING_SEGS) return -1;
    int mask = 0, next = 0;
    for (int i = 0; i < n; i++) {
        int s;
        if (is_settings[i]) {
            s = TT_RING_SETTINGS_SEG;
        } else {
            if (next == TT_RING_SETTINGS_SEG) next++;      // other apps skip the pinned slot
            s = next++;
        }
        if (s >= TT_RING_SEGS || (mask & (1 << s))) return -1;   // overflow or a second Settings
        seg_of[i] = (int8_t)s;
        mask |= 1 << s;
    }
    return mask;
}
