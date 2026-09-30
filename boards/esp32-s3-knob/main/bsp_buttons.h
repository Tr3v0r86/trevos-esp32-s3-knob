// bsp_buttons.h — T-Display-S3 two-button input (board-specific).
// BOOT on GPIO0, KEY on GPIO14, both active-low. This is the board's whole input
// surface (no touch, no wheel). TrevOS maps these to navigate + commit later.
//
// Task 8: main.c lives in THIS directory, so a quoted #include "bsp_buttons.h" from main.c
// always resolves to this file (the current-file-directory check runs before any -I search
// path, the same rule main.c's own top comment documents for bsp_display.h) - regardless of
// which board's SRCS pulls main.c in. That means a BSP_BTN3 board's own bsp_buttons.h never
// actually shadows this header for main.c itself (only for board-local files like its own
// bsp_buttons.c, which live in the board's own directory). So BSP_BTN_C has to be declared
// here too, gated on the same BSP_BTN3 define the board's CMakeLists.txt already sets, or
// on_btn's BSP_BTN3 arm fails with "undeclared identifier" on the one board that needs it.
#pragma once
#include <stdint.h>

typedef enum {
    BSP_BTN_BOOT = 0,
    BSP_BTN_KEY = 1,
#if BSP_BTN3
    BSP_BTN_C = 2,
#endif
} bsp_btn_t;
typedef enum { BSP_BTN_PRESS = 0, BSP_BTN_LONG = 1 } bsp_btn_evt_t;

// Fired from the button poll task. If it touches LVGL, take the lvgl_port lock.
typedef void (*bsp_btn_cb_t)(bsp_btn_t btn, bsp_btn_evt_t evt, void *ctx);

void bsp_buttons_init(bsp_btn_cb_t cb, void *ctx);
