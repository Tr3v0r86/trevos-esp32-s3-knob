/* sim/sim_touchtest.c — guaranteed-green touch test screen. See sim_touchtest.h.
 * Compiled into cydsim only. Validates the raw pointer indev end to end (clickable
 * widgets + drag readout) without booting the TrevOS shell. */
#include "sim_touchtest.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *s_coord;

static void zone_evt(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    const char *name = (const char *)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED) {
        fprintf(stderr, "[touchtest] CLICKED %s\n", name);
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_event_get_indev(e);
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        if (s_coord) lv_label_set_text_fmt(s_coord, "x=%d y=%d", (int)p.x, (int)p.y);
    }
}

void sim_touchtest_build(lv_obj_t *screen)
{
    static const char *names[3] = { "PREV", "COMMIT", "NEXT" };
    static const uint32_t cols[3] = { 0x2d4a6b, 0x3a6b2d, 0x6b2d4a };

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101010), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *z = lv_obj_create(screen);
        lv_obj_set_size(z, 74, 250);
        lv_obj_set_pos(z, 4 + i * 78, 4);
        lv_obj_set_style_bg_color(z, lv_color_hex(cols[i]), 0);
        lv_obj_set_style_radius(z, 6, 0);
        lv_obj_clear_flag(z, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lab = lv_label_create(z);
        lv_label_set_text(lab, names[i]);
        lv_obj_center(lab);

        lv_obj_add_event_cb(z, zone_evt, LV_EVENT_CLICKED, (void *)names[i]);
        lv_obj_add_event_cb(z, zone_evt, LV_EVENT_PRESSING, (void *)names[i]);
    }

    s_coord = lv_label_create(screen);
    lv_label_set_text(s_coord, "x=- y=-");
    lv_obj_set_pos(s_coord, 8, 290);
}
