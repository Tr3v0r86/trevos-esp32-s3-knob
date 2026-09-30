// bsp_haptic.h - the puck's DRV2605 LRA haptic, probed (E10).
//
// The DRV2605 shares the touch I2C bus and may be missing on a clone board, so init probes the
// address first. Absent = one "puck_bsp: haptic absent" log and every later call is a no-op, with
// no I2C traffic at all.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// DRV2605 ROM effect ids (library 6, LRA).
#define HAPTIC_THUNK 1   // strong click: a commit, an open, the chime
#define HAPTIC_TICK  7   // soft click: one wheel detent

// Non-fatal (E13): returns ESP_ERR_NOT_FOUND when nothing answers at I2C_ADDR_DRV2605.
esp_err_t bsp_haptic_init(void);

// Play one ROM effect now. No-op when the chip is absent or haptics are disabled.
// HAPTIC_TICK plays the effect set by bsp_haptic_set_tick (default HAPTIC_TICK itself).
void bsp_haptic_play(uint8_t effect);

// Master gate (the Settings pref lands in P3). Default on.
void bsp_haptic_set_enabled(bool enabled);

// True when a tick would actually be felt: the chip answered at init AND the master gate is on.
// The ring home reads it to decide whether a detent needs a visual flash instead (D18).
bool bsp_haptic_enabled(void);

// Replace the ROM effect that HAPTIC_TICK plays, to tune the wheel feel without touching callers.
void bsp_haptic_set_tick(uint8_t effect);
