// trevos_shot.c — dev serial screenshot via a flush-tap. See trevos_shot.h.
//
// We wrap the display flush callback that esp_lvgl_port installed. When a capture is
// armed we force a full-screen redraw; LVGL then flushes the screen as full-width
// bands, and we stream each band (region + base64 RGB565) over USB serial before
// passing it on to the real flush. The host (tools/grab-shot.py) reassembles a PNG.
// No large on-device buffer, no lv_snapshot (which deadlocks when run from the LVGL
// handler context) — we just tap the bytes already on their way to the panel.
#include "trevos_shot.h"
#include "lvgl.h"
#include "display/lv_display_private.h"   // read the original flush_cb to chain it
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <stdio.h>
#include <stddef.h>

static lv_display_t *s_disp;
static lv_display_flush_cb_t s_orig;
static volatile bool s_cap;
static int s_w, s_h;

static void b64_emit(const uint8_t *p, size_t n)
{
    unsigned char out[4104];
    for (size_t off = 0; off < n; ) {
        size_t c = n - off; if (c > 3072) c = 3072;  // 3-byte aligned -> padding only at end
        size_t ol = 0;
        if (mbedtls_base64_encode(out, sizeof(out), &ol, p + off, c) == 0)
            fwrite(out, 1, ol, stdout);
        off += c;
    }
}

static void flush_wrap(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    if (s_cap) {
        int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
        esp_log_level_set("*", ESP_LOG_NONE);   // keep logs out of the binary stream
        printf("\nTTREG %d %d %d %d\n", (int)a->x1, (int)a->y1, (int)a->x2, (int)a->y2);
        b64_emit(px, (size_t)w * h * 2);
        printf("\nTTREGEND\n");
        if (a->y2 >= s_h - 1 && a->x2 >= s_w - 1) { printf("\nTTFRAME_END\n"); s_cap = false; }
        fflush(stdout);
        esp_log_level_set("*", ESP_LOG_INFO);
    }
    s_orig(d, a, px);   // hand the band to the real esp_lvgl_port flush
}

void tt_shot_attach(lv_display_t *d)
{
    s_disp = d;
    s_w = lv_display_get_horizontal_resolution(d);
    s_h = lv_display_get_vertical_resolution(d);
    s_orig = d->flush_cb;
    lv_display_set_flush_cb(d, flush_wrap);
}

static void cap_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_disp || s_cap) return;
    printf("\nTTFRAME %d %d\n", s_w, s_h);
    fflush(stdout);
    s_cap = true;
    lv_obj_invalidate(lv_screen_active());   // force full redraw -> full-width bands flush
}

void tt_shot_init(void)
{
    // First capture ~6s after boot (let wifi + first render settle), then every 12s.
    lv_timer_t *first = lv_timer_create(cap_cb, 6000, NULL);
    lv_timer_set_repeat_count(first, 1);
    lv_timer_create(cap_cb, 12000, NULL);
}
