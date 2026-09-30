// motion_test.c - host test for tt_step / tt_arc_step (Task 5.12.1). pucksim only (SIM_WHEEL).
// SIM_MOTION_TEST=1 SIM_DISP=null ./sim/build/pucksim  ->  PASS motion, rc 0.
// Time is driven by hand: lv_tick_inc, then lv_anim_refr_now evaluates every tween at its own elapsed time.
#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#include "trevos.h"
#include "trevos_ui.h"

static int32_t g_v;
static int g_settled;
static void e(void *var, int32_t v) { (void)var; g_v = v; }
static void s(void *var) { (void)var; g_settled++; }

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "FAIL motion: %s (line %d, v=%d)\n", #c, __LINE__, (int)g_v); exit(1); } } while(0)
static void adv(uint32_t ms) { lv_tick_inc(ms); lv_anim_refr_now(); }

void motion_test(void)
{
    bool had_wheel = trev_has_wheel();
    trev_set_has_wheel(true);
    lv_obj_t *o = lv_obj_create(lv_screen_active());
    adv(2000);   // let the boot's own tweens finish, so the counts below are ours alone
    uint16_t base = lv_anim_count_running();

    // (1) 16 -> 0 over 150: the canvas's 40 and 80 percent keyframes read 6.9 and 1.0
    g_settled = 0;
    tt_step(o, e, 16, 0, 150, s);
    CHECK(g_v == 16 && lv_anim_count_running() == base + 1);
    adv(60);  fprintf(stderr, "(1) 60 ms: %d\n", (int)g_v);  CHECK(g_v == 6 || g_v == 7);
    adv(60);  fprintf(stderr, "(1) 120 ms: %d\n", (int)g_v); CHECK(g_v == 0 || g_v == 1);
    CHECK(g_settled == 0);
    adv(30);  fprintf(stderr, "(1) 150 ms: %d settled=%d\n", (int)g_v, g_settled); CHECK(g_v == 0 && g_settled == 1);
    CHECK(lv_anim_count_running() == base);

    // (2) retarget from the live value: one anim, no jump, lands on the new target, one settle in total
    g_settled = 0;
    tt_step(o, e, 0, 112, 150, s);
    adv(60);
    int32_t live = tt_step_live(o, e, 0);
    CHECK(live == g_v && live > 0 && live < 112);
    tt_step(o, e, live, 168, 150, s);
    fprintf(stderr, "(2) live %d, first value after retarget %d, anims +%d\n", (int)live, (int)g_v, lv_anim_count_running() - base);
    CHECK(lv_anim_count_running() == base + 1 && g_v == live);
    adv(149); CHECK(g_v < 168 && g_settled == 0);
    adv(1);   fprintf(stderr, "(2) 150 ms after retarget: %d settled=%d\n", (int)g_v, g_settled); CHECK(g_v == 168 && g_settled == 1);
    CHECK(tt_step_live(o, e, 7) == 7);   // no anim running: the rest value

    // (2b) tt_step_live reads the tween at the current tick, not at the last anim pass: 20 ms of tick
    // with no refresh must read the 80 ms value, and a retarget from it early-applies that value.
    tt_step(o, e, 0, 112, 150, NULL);
    adv(60);
    int32_t v60 = g_v;
    lv_tick_inc(20);
    live = tt_step_live(o, e, 0);
    fprintf(stderr, "(2b) 60 ms pass %d, live at +20 ms tick %d\n", (int)v60, (int)live);
    CHECK(g_v == v60 && live >= 79 && live <= 81);   // 112 * 0.714 at 80 ms of 150
    tt_step(o, e, live, 112, 150, NULL);
    CHECK(g_v == live);
    adv(150); CHECK(g_v == 112);

    // (3) a wheel scrub replaces a running 600 ms arc tween instead of racing it
    lv_obj_t *arc = lv_arc_create(lv_screen_active());
    lv_arc_set_range(arc, 0, 1000);
    tt_arc_anim(arc, 1000, 600);
    adv(100);
    tt_arc_step(arc, 500);
    fprintf(stderr, "(3) arc anims +%d\n", lv_anim_count_running() - base);
    CHECK(lv_anim_count_running() == base + 1);
    adv(150); fprintf(stderr, "(3) arc 150 ms after: %d\n", (int)lv_arc_get_value(arc)); CHECK(lv_arc_get_value(arc) == 500);
    CHECK(lv_anim_count_running() == base);

    // (4) no wheel, or ms 0: lands at once, settles once, starts no anim
    trev_set_has_wheel(false);
    g_settled = 0; g_v = 99;
    tt_step(o, e, 16, 0, 150, s);
    fprintf(stderr, "(4) no wheel: %d settled=%d anims +%d\n", (int)g_v, g_settled, lv_anim_count_running() - base);
    CHECK(g_v == 0 && g_settled == 1 && lv_anim_count_running() == base);
    trev_set_has_wheel(true);
    g_settled = 0; g_v = 99;
    tt_step(o, e, 16, 3, 0, s);
    CHECK(g_v == 3 && g_settled == 1 && lv_anim_count_running() == base);

    trev_set_has_wheel(had_wheel);
    lv_obj_delete(o); lv_obj_delete(arc);
    fprintf(stderr, "PASS motion\n");
}
