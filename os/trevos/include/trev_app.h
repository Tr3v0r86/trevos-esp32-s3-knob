// trev_app.h — the TrevOS app contract (board-neutral).
//
// Every TrevOS app implements this and registers at boot. The contract is the
// frozen seam between an app and the OS: the OS owns the screen and input; the app
// builds its UI under a root and reacts to two semantic inputs. A board maps its
// own hardware to those inputs (wheel + touch on the puck; two buttons on the T3),
// so apps never see a pin.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#define TREV_APP_API_VERSION 3

typedef struct trev_app trev_app_t;
typedef enum { TREV_TURN_PREV = -1, TREV_TURN_NEXT = 1 } trev_turn_t;

// A fourth semantic input, added in API v2: a swipe or an orientation flip. Optional —
// an app that has no use for it leaves on_gesture NULL and the OS falls back to a turn
// for the two swipe directions (see trev_input_gesture in trevos.c).
// TAP_CONTENT is appended LAST on purpose: every earlier value keeps its number, so a board
// or app built against the older enum is unaffected. It means "a tap landed on the content
// area, above the action bar", i.e. a press that is not one of the three bar verbs. A board
// that cannot tell content from chrome simply never sends it.
typedef enum {
    TREV_GESTURE_SWIPE_LEFT, TREV_GESTURE_SWIPE_RIGHT,
    TREV_GESTURE_SWIPE_DOWN, TREV_GESTURE_SWIPE_UP,
    TREV_GESTURE_FACE_DOWN,  TREV_GESTURE_FACE_UP,
    TREV_GESTURE_TAP_CONTENT
} trev_gesture_t;

typedef struct {
    uint32_t api_version;
    const char *id;
    const char *name;
    void (*on_start)(trev_app_t *app, lv_obj_t *root);   // build UI under root
    void (*on_stop)(trev_app_t *app);                    // drop widget pointers
    void (*on_tick)(trev_app_t *app, uint32_t now_ms);   // optional, may be NULL
    void (*on_turn)(trev_app_t *app, trev_turn_t dir);   // navigate / change a value
    void (*on_commit)(trev_app_t *app);                  // the commit action
    // v2, added at the end so a v1 literal still zero-initialises this to NULL and still
    // compiles: optional gesture input (swipe / face down-up). May be NULL.
    void (*on_gesture)(trev_app_t *app, trev_gesture_t g);
    // v3: optional direct finger movement, in display pixels; no release swipe afterward.
    void (*on_drag)(trev_app_t *app, int dx, int dy);
    // optional; NULL = a detent arrives as on_turn. A wheel board whose face also has a PREV bar
    // zone sets this so a tap and a detent stay different inputs. The bool means "felt": true
    // when the detent changed something the user can feel (selection moved, value changed),
    // false at a limit or with nothing to move to. trev_input_wheel returns it, and the board
    // ticks the haptic only on true. Appended after on_drag, so v3 defs stay valid (NULL).
    bool (*on_wheel)(trev_app_t *app, trev_turn_t dir);
} trev_app_def_t;

struct trev_app {
    const trev_app_def_t *def;
    lv_obj_t *root;
    void     *state;   // app-private; the app allocates in on_start, frees in on_stop
};
