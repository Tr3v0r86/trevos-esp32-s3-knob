/* sim/sim_touchtest.h — minimal LVGL touch test screen for cydsim.
 * Selected at runtime with SIM_SCREEN=touchtest. Builds three clickable tap zones
 * (PREV / COMMIT / NEXT) plus a live coordinate readout, so the sim + touch +
 * screenshot loop is provable independent of the TrevOS shell's layout debt. */
#pragma once
#include "lvgl.h"

void sim_touchtest_build(lv_obj_t *screen);
