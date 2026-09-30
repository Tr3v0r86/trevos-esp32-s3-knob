// bsp_wheel.h - the puck wheel, sampled at 1 ms off the LVGL task (C2).
//
// A PCNT unit counts the wheel; a 1 ms esp_timer decodes detents (wheel_decode.h) and queues
// them; the LVGL task drains the queue with bsp_wheel_pop and hands each to trev_input_wheel.
// The 8 ms LVGL poll this replaces missed 3 ms excursions.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Non-fatal (E13): on any failure logs "puck_bsp: wheel off (<err>)" and returns the error, so
// the caller simply skips the drain timer and the puck runs on touch alone.
esp_err_t bsp_wheel_init(void);

// Next queued detent (+1 / -1) into *detent; false when the queue is empty. 0-timeout.
bool bsp_wheel_pop(int *detent);
