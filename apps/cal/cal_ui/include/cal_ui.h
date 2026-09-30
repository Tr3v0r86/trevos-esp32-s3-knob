// cal_ui.h - the Today-only calendar TrevOS app: time-proportional cards, a now-line,
// event details and a return-to-now action. No navigation to another date.
#pragma once
#include <stdbool.h>
#include "trev_app.h"

extern const trev_app_def_t CAL_APP;

// Chime check, called at 1 Hz by the board whether or not the face is mounted. Keeps its own
// copy of today refreshed from cal_sync and returns true exactly once per timed event, at
// start minus five minutes. The board plays the sound; this decides.
bool cal_chime_due(void);
