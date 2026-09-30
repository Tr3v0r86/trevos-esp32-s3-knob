// led_strip.h — sim shim for espressif/led_strip. Same call surface app_ledstrip.c uses.
//
// The stub keeps the pixel state and prints transitions, so a headless sim run says what
// the strip WOULD be doing. That is the part the PNG cannot show: the screen renders the
// same whether the RMT calls landed or not.
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct led_strip_t *led_strip_handle_t;

typedef enum { LED_MODEL_WS2812 = 0, LED_MODEL_SK6812 = 1 } led_model_t;
typedef enum { LED_STRIP_COLOR_COMPONENT_FMT_GRB = 0, LED_STRIP_COLOR_COMPONENT_FMT_RGB = 1 }
        led_color_component_format_t;
typedef enum { RMT_CLK_SRC_DEFAULT = 0 } rmt_clock_source_t;

typedef struct {
    int         strip_gpio_num;
    uint32_t    max_leds;
    led_model_t led_model;
    led_color_component_format_t color_component_format;
    struct { uint32_t invert_out: 1; } flags;
} led_strip_config_t;

typedef struct {
    rmt_clock_source_t clk_src;
    uint32_t           resolution_hz;
    struct { uint32_t with_dma: 1; } flags;
} led_strip_rmt_config_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *sc,
                                   const led_strip_rmt_config_t *rc,
                                   led_strip_handle_t *out);
esp_err_t led_strip_set_pixel(led_strip_handle_t h, uint32_t i, uint32_t r, uint32_t g, uint32_t b);
esp_err_t led_strip_refresh(led_strip_handle_t h);
esp_err_t led_strip_clear(led_strip_handle_t h);
