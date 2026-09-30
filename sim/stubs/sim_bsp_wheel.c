/* sim/stubs/sim_bsp_wheel.c: host bsp_wheel. There is no PCNT and no 1 ms sampler, so init
 * fails the way a board without a wheel does (E13): main.c logs nothing, creates no
 * wheel_drain_cb timer, and the queue is never read. The sim feeds detents itself: `TURN +1|-1`
 * on stdin calls trev_input_wheel from sim_touch.c, under the LVGL lock, with the same
 * haptic tick wheel_drain_cb plays. pop is therefore always empty. */
#include "bsp_wheel.h"

esp_err_t bsp_wheel_init(void) { return ESP_FAIL; }

bool bsp_wheel_pop(int *detent) { (void)detent; return false; }
