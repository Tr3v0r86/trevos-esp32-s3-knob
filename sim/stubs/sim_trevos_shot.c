/* sim/stubs/sim_trevos_shot.c — host screenshot stub. The SDL window (or nothing, in
 * headless mode) is the surface; the serial-tap capture path is not needed. */
#include "trevos_shot.h"
void tt_shot_attach(lv_display_t *disp) { (void)disp; }
void tt_shot_init(void) { }
