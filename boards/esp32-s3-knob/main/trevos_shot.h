// trevos_shot.h — dev-only serial screenshot. Snapshots the active LVGL screen and
// streams it base64 over USB serial; tools/grab-shot.py on the host saves a PNG.
// Lets the agent verify the glass itself instead of asking for phone photos.
// Remove (or gate off) for production builds.
#pragma once
#include "lvgl.h"

// Wrap the display's flush callback so captures can tap the panel bytes. Call once
// right after lvgl_port_add_disp(), passing the returned display.
void tt_shot_attach(lv_display_t *disp);

// Create the periodic dev-screenshot timer (first frame a few seconds after boot,
// then every period). Call once, under the lvgl_port lock.
void tt_shot_init(void);
