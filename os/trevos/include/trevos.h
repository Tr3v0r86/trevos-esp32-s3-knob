// trevos.h — TrevOS runtime API.
//
// The OS owns one LVGL screen and routes semantic input to the active app. A board
// brings up LVGL, calls trev_init() with the screen, registers apps, and feeds
// trev_input_* from its own input glue. Input and open/home calls must be made
// under the LVGL lock (they touch widgets); on_tick runs inside the LVGL task.
#pragma once
#include "lvgl.h"
#include "trev_app.h"

void trev_init(lv_obj_t *screen);
void trev_app_register(const trev_app_def_t *def);
int  trev_app_count(void);
const trev_app_def_t *trev_app_def(int index);   // registered app metadata, or NULL if out of range

void trev_open(int index);            // mount app[index] on the screen

// Designate a registered app as the home face (a styled launcher). When set,
// trev_input_home() opens it instead of the bare built-in stub. -1 (default) = stub.
void trev_set_home_app(int index);

void trev_input_turn(trev_turn_t dir);
void trev_input_commit(void);
void trev_input_home(void);           // open the home app (if set) or the stub

// Routes to the active app's on_gesture if it has one. If not, SWIPE_LEFT/SWIPE_RIGHT
// fall back to trev_input_turn(NEXT/PREV) so a board without gesture-aware apps still
// gets basic navigation from a swipe; every other gesture is dropped silently.
void trev_input_gesture(trev_gesture_t g);

// A content tap WITH its position. Boards call this instead of trev_input_gesture(TAP_CONTENT)
// so a face that lists things (the calendar's rows) can hit-test which one was pressed; the
// point is readable inside on_gesture via trev_last_tap. Display coordinates, already
// flipped by the board when the panel is upside down.
void trev_input_content_tap(int x, int y);
void trev_last_tap(int *x, int *y);

// The status rail. A board that draws more than a clock on the rail (the disk: clock, sync,
// pending, battery) registers its two functions once; a face calls trev_rail_init on the
// label tt_statusbar hands it and trev_rail_update from its tick. Unregistered, the rail is
// a plain HH:MM (--:-- until a clock source lands).
void trev_set_rail(void (*init)(lv_obj_t *label), void (*update)(lv_obj_t *label));
void trev_rail_init(lv_obj_t *label);
void trev_rail_update(lv_obj_t *label);

// Idle clock for dimming: any touch calls trev_input_note_activity(); trev_idle_ms()
// reports how long it has been since. Stamped once at trev_init so the device reads as
// active at boot, not already idle.
void trev_input_note_activity(void);
uint32_t trev_idle_ms(void);

// Direct-touch apps own the full glass, including the former action-bar area.
bool trev_has_direct_touch(void);
void trev_input_drag(int dx, int dy);

// Wheel input (C9). A detent is +1 / -1 (any sign counts). Returns true when it was delivered
// as a turn, false when the screen was dark and the detent only woke it (E6), so the board
// can skip its tick haptic. Call under the LVGL lock, like every trev_input_*.
bool trev_input_wheel(int detent);
void trev_set_wheel_invert(bool invert);

// Wheel presence, a runtime seam owned by the OS (BSP_HAS_WHEEL is private to the board's main
// component, so the trevos component cannot compile on it). A board with a wheel calls
// trev_set_has_wheel(true) once at boot; faces branch on trev_has_wheel(). Default false.
void trev_set_has_wheel(bool has);
bool trev_has_wheel(void);

// The one dark threshold (E6): idle past this and the backlight is off, so the first touch or
// detent only wakes. Owned here so the disk's dimmer, its touch driver and the wheel adapter
// cannot disagree. 0 (default) = the board never goes dark.
void trev_set_dark_ms(uint32_t ms);
uint32_t trev_dark_ms(void);

// Feedback hook: a board that has haptics registers one function and TrevOS calls it on a
// commit and on an app open, without knowing what a haptic is. NULL (default) = no-op.
typedef enum { TREV_FB_COMMIT, TREV_FB_OPEN } trev_feedback_t;
void trev_set_feedback(void (*fn)(trev_feedback_t));
void trev_feedback(trev_feedback_t fb);   // a face asks for the board's feedback (a direct-touch face never gets COMMIT from trev_input_commit); no-op without a hook
