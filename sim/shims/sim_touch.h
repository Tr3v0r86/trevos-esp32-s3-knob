/* sim/shims/sim_touch.h — host touch input for cydsim (the CYD has a touchscreen,
 * not the t3's two buttons). A pointer indev fed from stdin on a SEPARATE thread,
 * mirroring sim_buttons.c's cross-thread shape.
 *
 * Deliberately NOT lvgl_port_add_touch: the real device facade takes an
 * esp_lcd_touch_handle_t (xpt2046) the host has no equivalent for. cydsim calls
 * sim_touch_init() directly after the display exists.
 *
 * Grammar (one command per stdin line), coords are 240x320 display-space pixels:
 *   TAP x y    one tap (press then release across ticks) + a zone->verb
 *   DOWN x y   pointer press at (x,y)
 *   MOVE x y   pointer move while pressed (drag step)
 *   UP x y     pointer release
 *   WAIT ms    hold the verb queue ms of LVGL time since the last verb (pacing, not input)
 *   HOME       trev_input_home()
 *   QUIT       exit
 *
 * The current TrevOS shell faces are driven only by trev_input_turn/commit/home,
 * not raw clicks, so cydsim ALSO maps a TAP's x to a semantic verb (left third =
 * PREV, right third = NEXT, middle = COMMIT). Those verbs are applied by
 * sim_touch_pump(), which sim_main calls each tick UNDER the lvgl_port lock — never
 * from the stdin thread. The raw pointer still feeds LVGL so clickable widgets
 * (e.g. the touchtest screen) work end to end.
 */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void sim_touch_init(lv_display_t *disp);   /* register the pointer indev on disp */
void sim_touch_pump(void);                 /* drain queued verbs -> trev_input_*; call under lvgl_port_lock */
/* true once stdin is closed, the verb queue is empty and at least one input verb was applied;
 * *last_ms = lv_tick_get() when the last one was. A WAIT is pacing, not a verb. Used by
 * SIM_SHOT_AFTER_MS (sim_main.c). */
bool sim_touch_settled(uint32_t *last_ms);
