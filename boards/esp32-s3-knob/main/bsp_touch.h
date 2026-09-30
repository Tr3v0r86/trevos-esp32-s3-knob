// bsp_touch.h — CST816S capacitive touch on the 1.85B (ADR-0012 sequence step 5).
//
// Raw coordinates only. No TrevOS verbs and no zone mapping live here: the zone binding is
// step 6 work and it MUST go through tt_zone_at() (STATE.md §5), which needs LVGL to exist
// first. Deriving zones twice is exactly how the CYD's action-bar labels and its hit-zones
// drifted 23px apart and a press on a visible label fired the neighbouring verb.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Reset the touch panel, scan the shared I2C bus (every responder is logged, which is also
// the first look at the RTC / codec / IMU that later stages need), then create the CST816S.
// In the shell build this also starts the poll task, which calls trev_input_* itself, so the
// shared main.c needs no callback. The bringup card (BSP_BRINGUP) gets no task and polls
// bsp_touch_read() on its own.
esp_err_t bsp_touch_init(void);

// Current touch, if any. Display pixels, 0..LCD_H_RES-1 / 0..LCD_V_RES-1.
bool bsp_touch_read(int *x, int *y);

// Flip touch coordinates 180 degrees to match a flipped display. Mechanism only - a later
// task (the IMU) decides when to call this. Idempotent.
void bsp_touch_set_flipped(bool flipped);
