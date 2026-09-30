/* sim/stubs/sim_bsp_display.c — host BSP display. No panel; the real LVGL display is
 * created in lvgl_port_add_disp. Hands back NULL opaque handles (never dereferenced).
 *
 * Deliberately does NOT `#include "bsp_display.h"`: this file lives in sim/stubs/, which has
 * no same-directory copy, so a quoted include falls through to each target's -I search order
 * instead of resolving unambiguously the way main.c's own include does. Only papersim is a
 * problem here (t3sim/cydsim/roundsim dropped boards/papercolor/main from their include path
 * entirely, Task 7 review finding 3, so their search never reaches the page at all): papersim
 * still needs boards/papercolor/main ahead of ${MAIN} for its own bsp_buttons.h (three
 * buttons) and sim_main.c's guarded spectra6.h, and that same ordering means its search would
 * also silently pick up boards/papercolor/main/bsp_display.h - a DIFFERENT bsp_display_init
 * signature (no io/panel out-params) than the one this file defines below and every sim
 * target actually calls. Irreducible at the CMake layer for papersim (see its comment in
 * sim/CMakeLists.txt), so this file pulls the three shim typedefs directly instead: they are
 * the only things it needs, and every one of them is in shims/, which is first on every
 * target's search path regardless of board ordering.
 */
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t bsp_display_init(esp_lcd_panel_io_handle_t *ret_io, esp_lcd_panel_handle_t *ret_panel)
{
    if (ret_io)    *ret_io = NULL;
    if (ret_panel) *ret_panel = NULL;
    return ESP_OK;
}

void bsp_display_backlight_on(void) { /* no panel to light */ }

#ifdef BSP_HAS_BACKLIGHT_DIM
// main.c forward-declares this under the same guard (a quoted bsp_display.h resolves to the
// T3's). The sim has no LEDC channel to program; the dimmer's decision is still exercised.
esp_err_t bsp_backlight_set(uint8_t percent) { (void)percent; return ESP_OK; }
#endif

#ifdef BSP_HAS_IMU
// T9: main.c's `#include "bsp_display.h"` always resolves to the T-Display-S3's own header
// (a quoted include searches the including file's directory first), so both of these are
// forward-declared directly in main.c under their own BSP_HAS_* guards rather than pulled in
// from a header - see its comment just above app_main(). Nothing to include here, only
// definitions to provide so the link succeeds.
#include "lvgl.h"

// The sim has no panel to rotate, so this proves auto-flip's call path fires without
// pretending to a visual rotation the host never renders. Skipped: an actual frame flip, and
// the "flip" shot that would need one (see shoot.sh and task-9-report.md).
void bsp_display_set_flipped(lv_display_t *disp, bool flipped)
{
    (void)disp; (void)flipped;
}

// Not reachable from any header on this build either: BSP_INPUT_TOUCH is 0 on every sim
// target (main.c's own `#if BSP_INPUT_TOUCH` skips `#include "bsp_touch.h"`), yet flip_set()
// calls this unconditionally under BSP_HAS_IMU regardless. This definition exists purely to
// satisfy that one call - see "main.c findings" in task-9-report.md.
void bsp_touch_set_flipped(bool flipped)
{
    (void)flipped;
}
#endif
