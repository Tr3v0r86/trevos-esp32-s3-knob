// bsp_display.h - puck display BSP (board-specific). Nothing above this layer sees a pin.
// Same names and signatures as the 1.85B disk's, so the shared main.c calls it unchanged.
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "board_pins.h"

// Native square 360x360. The shared main.c reads these (-D'd in main/CMakeLists.txt) and owns
// rotation via esp_lvgl_port, so the BSP sets NO swap/mirror itself.
#ifndef BSP_LCD_H_RES
#define BSP_LCD_H_RES LCD_H_RES
#endif
#ifndef BSP_LCD_V_RES
#define BSP_LCD_V_RES LCD_V_RES
#endif

// Bring up the LEDC backlight (left OFF), the QSPI bus, and the ST77916. The panel is left
// display-on but dark, so the first frame can be pushed before the backlight comes up and
// there is no white flash at boot.
esp_err_t bsp_display_init(esp_lcd_panel_io_handle_t *ret_io, esp_lcd_panel_handle_t *ret_panel);

// 0..100. The FET gate is active high.
esp_err_t bsp_backlight_set(uint8_t percent);

// What the shared main.c calls once the first frame is up. Thin wrapper over
// bsp_backlight_set(100).
void bsp_display_backlight_on(void);
