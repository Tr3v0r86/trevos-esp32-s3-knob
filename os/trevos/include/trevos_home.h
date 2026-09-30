// trevos_home.h - the round-native home face (TrevOS app). Round builds only: the whole
// implementation is #if TT_ROUND_DISPLAY, so referencing TREV_HOME_ROUND on a rectangular
// board is a link error by design (those boards have their own launchers).
//
// X7 / E12 (ADR-0020): TREV_HOME_ROUND has two implementations, chosen by one value seam.
// TT_HOME_RING=0 (default, the disk): trevos_home_round.c, a clock and two pills.
// TT_HOME_RING=1 (the puck): trevos_home_ring.c, the ring. Each file is #if-empty under the
// other setting and both are always in the build, so flipping a board is one define.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "trev_app.h"

#ifndef TT_HOME_RING
#define TT_HOME_RING 0
#endif

extern const trev_app_def_t TREV_HOME_ROUND;

// Ring home hooks (TT_HOME_RING=1 only; the definitions live in trevos_home_ring.c). The board
// registers them once, before the home opens. Each is optional: unset, the disc shows what it
// can (a blank one-liner, a blank status, no flash).
//
// status: writes the disc's status line: "HH:MM", "~HH:MM" or "--:--", and when an alert
// applies " · <alert>" after it (the low-battery "12% LOW"). Text after the first " · " is
// drawn in TT_INK, the rest in TT_DESC. Called about once a second while the ring is up.
void trev_ring_set_status(void (*fn)(char *line, size_t cap));
// one-liner: the second disc line for the app whose registry id is app_id (a string literal:
// the pointer is kept). Called about once a second while that app is selected.
void trev_ring_set_oneliner(const char *app_id, void (*fn)(char *buf, size_t cap));
// haptics: true when a detent will actually be felt. False makes each detent flash the newly
// selected segment to ink for 80 ms instead (D18).
void trev_ring_set_haptics_on(bool (*fn)(void));
