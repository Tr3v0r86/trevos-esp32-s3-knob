// trev_ring.h - ring home geometry (D5, D6, D13). Pure: no LVGL, so it is host-testable and
// the ring draws from the same table it hit-tests with. Angles run clockwise from 12 o'clock.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TT_RING_SEGS 8
#define TT_RING_DISC 8          /* hit result for the centre disc */
#define TT_RING_SETTINGS_SEG 4  /* 6 o'clock (D5) */

typedef struct { int cx, cy, r_disc, r_in, r_out, r_hit; } tt_ring_geom_t;
extern const tt_ring_geom_t TT_RING;                      /* {180, 180, 118, 126, 172, 180} */

int  tt_ring_hit(int x, int y);                           /* 0..7, TT_RING_DISC, or -1 */
int  tt_ring_next(int sel, int dir, uint8_t occupied);    /* -1 when the mask is empty */
void tt_ring_seg_centre(int seg, int r, int *x, int *y);
int  tt_ring_slots(const bool *is_settings, int n, int8_t seg_of[]);   /* occupied mask; -1 if n > 8 */
