/* sim shim: esp_lvgl_port.h — mirrors the real API surface main.c uses (app_main).
 * Field layout matches the designated initializers in main.c (extra fields ignored).
 * Backed by sim/stubs/sim_lvgl_port.c (recursive mutex + display create). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

typedef struct {
    int task_priority;
    int task_stack;
    int task_affinity;
    int task_max_sleep_ms;
    unsigned task_stack_caps;
    int timer_period_ms;
} lvgl_port_cfg_t;

#define ESP_LVGL_PORT_INIT_CONFIG()   \
    {                                 \
        .task_priority = 4,           \
        .task_stack = 7168,           \
        .task_affinity = -1,          \
        .task_max_sleep_ms = 500,     \
        .task_stack_caps = 0,         \
        .timer_period_ms = 5,         \
    }

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    uint32_t buffer_size;
    bool double_buffer;
    uint32_t hres;
    uint32_t vres;
    bool monochrome;
    struct { bool swap_xy; bool mirror_x; bool mirror_y; } rotation;
    struct { unsigned buff_dma : 1; unsigned buff_spiram : 1; unsigned swap_bytes : 1; } flags;
} lvgl_port_display_cfg_t;

esp_err_t      lvgl_port_init(const lvgl_port_cfg_t *cfg);
lv_display_t  *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg);
bool           lvgl_port_lock(uint32_t timeout_ms);   /* 0 = block forever */
void           lvgl_port_unlock(void);
