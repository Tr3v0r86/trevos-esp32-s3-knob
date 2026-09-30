// trevos_settings.h - the OS-owned Settings face (round builds only, like trevos_home.h:
// the whole implementation is #if TT_ROUND_DISPLAY, so referencing TREV_SETTINGS on a
// rectangular board is a link error by design). The face renders a rows array a board binds
// (P3, E14 as revised by C8); with nothing bound it shows a default set (About, Chip, Free
// heap, Uptime as INFO rows, Restart as a CONFIRM row) so a board that never binds still has a
// working Settings.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "trev_app.h"

extern const trev_app_def_t TREV_SETTINGS;

typedef enum { TREV_ROW_VALUE, TREV_ROW_TOGGLE, TREV_ROW_ACTION, TREV_ROW_CONFIRM, TREV_ROW_INFO } trev_row_kind_t;
typedef struct {
    const char *label;
    trev_row_kind_t kind;
    int  (*get)(void);                     /* VALUE, TOGGLE */
    void (*set)(int v);                    /* applies live */
    void (*act)(void);                     /* ACTION, CONFIRM */
    void (*text)(char *buf, size_t cap);   /* value or info text */
    int16_t min, max, step;
    uint8_t group;                         /* 0 essentials, 1 ADVANCED */
} trev_setting_row_t;

// Bind the rows the face renders. The array must outlive the face (a board's static table);
// it is not copied. The board orders essentials (group 0) before ADVANCED (group 1) rows; the
// face does not sort. The ADVANCED eyebrow goes before the first group-1 row.
// Callable before or after the face is built; a built face rebuilds its rows and drops any open or armed row.
void trev_settings_bind(const trev_setting_row_t *rows, int n);

// The face never persists: a row's `set` applies live, and `fn` is the board's save hook. It
// fires once, 1 s after the last change (VALUE scrub or TOGGLE flip), and immediately when an
// open row closes or the face stops with a change pending. The board wires it to trev_pref_set.
void trev_settings_set_idle_cb(void (*fn)(void));
