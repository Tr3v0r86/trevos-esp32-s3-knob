/* sim/stubs/sim_lvgl_port.c — host esp_lvgl_port.
 *
 * Faithful to the device contract that matters for the focus-freeze:
 *   - one RECURSIVE mutex (matches xSemaphoreCreateRecursiveMutex). on_btn (btn thread)
 *     and the sim_main LVGL loop both take it; recursive so pomo_render/set_view can
 *     nest under the on_btn lock.
 *   - lvgl_port_lock(0) blocks forever (portMAX_DELAY); lock(N) times out -> false,
 *     preserving on_btn's early-return-on-false.
 *   - does NOT call lv_init (sim_main owns lv_init + the lv_timer_handler loop), and does
 *     NOT spawn an LVGL task (the loop is the LVGL task).
 *   - lvgl_port_add_disp creates the display: headless null (SIM_DISP=null) or a real SDL
 *     window (default), so the same main.c runs head-full for a human or headless for an agent.
 */
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t s_lock;
static int s_ready = 0;

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg)
{
    (void)cfg;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lock, &a);
    pthread_mutexattr_destroy(&a);
    s_ready = 1;
    return ESP_OK;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if (!s_ready) return true;
    if (timeout_ms == 0) {
        pthread_mutex_lock(&s_lock);   /* block forever — portMAX_DELAY */
        return true;
    }
    /* macOS has no pthread_mutex_timedlock; spin trylock to the deadline. */
    const uint32_t step_us = 1000;
    uint32_t waited_us = 0;
    while (pthread_mutex_trylock(&s_lock) != 0) {
        if (waited_us >= timeout_ms * 1000u) return false;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = step_us * 1000 };
        nanosleep(&ts, NULL);
        waited_us += step_us;
    }
    return true;
}

void lvgl_port_unlock(void)
{
    if (s_ready) pthread_mutex_unlock(&s_lock);
}

/* ---- display ---- */
/* 320 is a max-width assumption: safe for cydsim's 240x320 (240*40*2 < this). */
static uint8_t s_nullbuf[320 * 40 * 2];   /* partial RGB565 draw buffer for headless */
static void null_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)px;
    /* SIM_FLUSH_LOG: one line per flushed area, so shoot.sh can bound idle redraws. */
    static int log_flush = -1;
    if (log_flush < 0) log_flush = getenv("SIM_FLUSH_LOG") != NULL;
    if (log_flush) fprintf(stderr, "[sim] flush %d %d %d %d\n", (int)a->x1, (int)a->y1, (int)a->x2, (int)a->y2);
    lv_display_flush_ready(d);
}

lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg)
{
    const char *mode = getenv("SIM_DISP");
    int headless = mode && strcmp(mode, "null") == 0;
    lv_display_t *d;
    if (headless) {
        d = lv_display_create((int32_t)cfg->hres, (int32_t)cfg->vres);
        lv_display_set_buffers(d, s_nullbuf, NULL, sizeof(s_nullbuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(d, null_flush);
        fprintf(stderr, "[port] headless display %ux%u\n", cfg->hres, cfg->vres);
    } else {
        d = lv_sdl_window_create((int32_t)cfg->hres, (int32_t)cfg->vres);
        if (d) {
            lv_sdl_window_set_zoom(d, 3);
            lv_sdl_window_set_title(d, "TrevOS sim (t3)");
            fprintf(stderr, "[port] SDL display %ux%u zoom 3\n", cfg->hres, cfg->vres);
        } else {
            fprintf(stderr, "[port] FATAL: SDL window create failed (no GUI? try SIM_DISP=null)\n");
        }
    }
    return d;
}
