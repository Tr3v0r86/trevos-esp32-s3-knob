/* sim/sim_main.c — host entry point for the TrevOS / Pomodoist simulator.
 *
 * Owns lv_init + the LVGL loop (= the device's "LVGL task"). Delegates all bring-up to the
 * board's real app_main (renamed board_app_main via -Dapp_main=board_app_main) so the exact
 * device boot order runs: nvs -> bsp -> lvgl_port_init (mutex) -> lvgl_port_add_disp (display)
 * -> core -> trev_init/register/open -> bg timer -> bsp_buttons_init (spawns the btn thread).
 *
 * The loop takes the lvgl_port lock around lv_timer_handler, exactly as esp_lvgl_port does on
 * device, so the cross-thread lock contention with on_btn (btn thread) is reproduced here.
 *
 * Two targets share this file. t3sim: buttons, 320x170. cydsim (-DCYDSIM=1): the CYD's
 * touch model — a pointer indev (sim_touch) at 240x320, with tap-zones mapped to the
 * TrevOS verbs since the current shell faces are not clickable. CYDSIM also offers
 * SIM_SCREEN=touchtest: a standalone clickable test screen that proves the touch +
 * screenshot loop without the shell. CYDSIM is a PER-TARGET define (only cydsim sets it),
 * which is why every #ifdef CYDSIM block leaves t3sim byte-for-byte unchanged.
 *
 * Display: real SDL window by default; headless null display with SIM_DISP=null (agent/CI).
 * SIM_TICKS=N caps the loop for automated runs (default: run forever).
 * SIM_SHOT=out.ppm writes the active screen as a binary PPM at exit (needs LV_USE_SNAPSHOT).
 */
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#ifdef CYDSIM
#include "sim_touch.h"
#include "sim_touchtest.h"
#endif
#ifdef SIM_SPECTRA6
#include "spectra6.h"
#endif

void board_app_main(void);   /* main.c's app_main, renamed at compile time */

#if defined(TT_HAS_OUTBOX) && TT_HAS_OUTBOX
#include "pomodoist_outbox.h"

/* T9: seed today's outbox so build_ledger_round (main.c) has real rows to draw instead of
 * "No pomos yet today". seed_demo()'s tasks (main.c) never get an id - seed_task() sets
 * title/project/tags/desc/pomos/priority only - so any id pushed here matches nothing in
 * the live tasklist and every row falls to the "last 4 chars" fallback rather than a title.
 * That is the honest reading for a task that has since dropped off the list, and it is also
 * this stub's only option short of editing main.c to give seed_demo ids.
 *
 * Two items for "seedA001", one for "seedB002", 25 minutes each, then one pop-to-sent so the
 * ledger shows both a pending count and a sent row: after the pop, pending = {seedA001,
 * seedB002} (count 2) and sent = {seedA001}, so the ledger aggregates 2x seedA001 + 1x
 * seedB002 = 75 total minutes, 2 pending. */
static void sim_seed_outbox(void)
{
    time_t now = time(NULL);   // ponytail: no midnight-rollover guard - these are minutes-old, not a real risk
    outbox_item_t a1 = { .ended_utc = now - 3600, .minutes = 25 };
    outbox_item_t a2 = { .ended_utc = now - 1800, .minutes = 25 };
    outbox_item_t b1 = { .ended_utc = now -  900, .minutes = 25 };
    strncpy(a1.task_id, "seedA001", sizeof a1.task_id - 1);
    strncpy(a2.task_id, "seedA001", sizeof a2.task_id - 1);
    strncpy(b1.task_id, "seedB002", sizeof b1.task_id - 1);
    outbox_push(&a1);
    outbox_push(&a2);
    outbox_push(&b1);
    outbox_pop_sent();   // oldest pending (a1) -> sent ring
}
#endif

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

#if LV_USE_SNAPSHOT
/* SIM_SHOT=path.ppm -> dump the active screen as a binary PPM (P6). Works headless
 * (lv_snapshot renders the object tree to its own buffer, independent of the display
 * flush). Convert to PNG with: sips -s format png path.ppm --out path.png  (macOS). */
static void sim_write_ppm(const char *path)
{
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!snap) { fprintf(stderr, "[shot] snapshot failed\n"); return; }
    int w = (int)snap->header.w, h = (int)snap->header.h;
    uint32_t stride = snap->header.stride;
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[shot] cannot open %s\n", path); lv_draw_buf_destroy(snap); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = snap->data + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
#if TT_ROUND_DISPLAY
            // Optional presentation of the physical glass. Raw shots remain the bounds gate.
            if(getenv("SIM_GLASS") && (x-w/2)*(x-w/2)+(y-h/2)*(y-h/2)>(w/2)*(w/2)) {
                fputc(24,f);fputc(24,f);fputc(24,f);continue;
            }
#endif
            const uint8_t *p = row + (size_t)x * 4;   /* ARGB8888 little-endian = B,G,R,A */
#ifdef SIM_SPECTRA6
            /* The page's panel can show six colours and nothing else, so the shot must show
             * six colours and nothing else. Anything softer is a sim that lies, and the sim
             * is the ADR-0014 gate. Same nearest-colour map the device runs in epd_fastest. */
            uint8_t qr, qg, qb;
            spectra6_rgb(spectra6_index(p[2], p[1], p[0]), &qr, &qg, &qb);
            fputc(qr, f); fputc(qg, f); fputc(qb, f);
#else
            fputc(p[2], f); fputc(p[1], f); fputc(p[0], f);
#endif
        }
    }
    fclose(f);
    lv_draw_buf_destroy(snap);
    fprintf(stderr, "[shot] wrote %s (%dx%d ppm)\n", path, w, h);
}
#endif

int main(void)
{
#if defined(TT_DEV_CAL_NOW) && TT_DEV_CAL_NOW >= 0
    extern void sim_clock_set(time_t);
    time_t fixed=time(NULL);struct tm tm;localtime_r(&fixed,&tm);
    tm.tm_hour=TT_DEV_CAL_NOW/60;tm.tm_min=TT_DEV_CAL_NOW%60;tm.tm_sec=0;
#if defined(TT_DEV_CAL_DATE)   /* YYYYMMDD: pin the date too (weekday and "tomorrow" follow); opt-in */
    tm.tm_year=TT_DEV_CAL_DATE/10000-1900;tm.tm_mon=TT_DEV_CAL_DATE/100%100-1;tm.tm_mday=TT_DEV_CAL_DATE%100;
#endif
    sim_clock_set(mktime(&tm));
#endif
    if(getenv("SIM_FAIL_FRAME")) return 23;
    fprintf(stderr, "[sim] lv_init\n");
    lv_init();

#if defined(TT_HAS_OUTBOX) && TT_HAS_OUTBOX
    sim_seed_outbox();   // before board_app_main: outbox_push/pop_sent don't need outbox_init
#endif

#ifdef CYDSIM
    int pump = 0;
    const char *screen = getenv("SIM_SCREEN");
    if (screen && strcmp(screen, "touchtest") == 0) {
        /* Standalone touch test (no shell). lvgl_port_init is REQUIRED here: board_app_main
         * normally creates the recursive mutex, but this branch skips it, and the loop below
         * locks it every tick. */
        fprintf(stderr, "[sim] SIM_SCREEN=touchtest (240x320, no shell)\n");
        lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
        lvgl_port_init(&pc);
        lvgl_port_display_cfg_t dc = { .hres = 240, .vres = 320, .buffer_size = 240 * 40 };
        lv_display_t *disp = lvgl_port_add_disp(&dc);
        lvgl_port_lock(0);
        sim_touchtest_build(lv_screen_active());
        lvgl_port_unlock();
        sim_touch_init(disp);
    } else {
        fprintf(stderr, "[sim] cyd shell (board_app_main @ 240x320) + touch indev\n");
        board_app_main();
        sim_touch_init(lv_display_get_default());
        pump = 1;   /* shell ignores raw clicks; tap-zones drive trev_input_* via the pump */
    }
#else
    fprintf(stderr, "[sim] board_app_main (device boot order)\n");
    board_app_main();
#endif

#if defined(TT_CAL) && TT_CAL && !CAL_FACE_RECT   // the polish test drives the round face only
    if(getenv("SIM_CAL_POLISH_TEST")) { extern void cal_polish_test(void);cal_polish_test();return 0; }
#endif
#ifdef SIM_WHEEL   // only pucksim links sim/motion_test.c (Task 5.12.1)
    if(getenv("SIM_MOTION_TEST")) { extern void motion_test(void); motion_test(); return 0; }
#endif
    const char *cap = getenv("SIM_TICKS");
    int ticks = cap ? atoi(cap) : -1;
    fprintf(stderr, "[sim] LVGL loop start (ticks=%s)\n", cap ? cap : "inf");

    uint32_t last = now_ms();
    /* SIM_TICK_MS=n: LVGL time advances exactly n ms per loop instead of the real elapsed
     * time, so timer-driven faces (the break countdown) shoot byte-identically run to run.
     * Unset = real time, as before. sim/identity.sh sets it. */
    const char *tick_ms = getenv("SIM_TICK_MS");
#ifdef CYDSIM
    /* SIM_SHOT_AFTER_MS=n: end the loop n ms of LVGL time after the script's last input verb was
     * applied (a WAIT is not one), so a mid-tween frame is pinned in LVGL time. Needs SIM_TICK_MS
     * with n a multiple of it, else the frame would land on a tick the script cannot name. */
    long shot_after = -1;
    if (getenv("SIM_SHOT_AFTER_MS")) {
        const char *a = getenv("SIM_SHOT_AFTER_MS"); char *end = (char *)a;
        long n = (*a >= '0' && *a <= '9') ? strtol(a, &end, 10) : 0, tk = tick_ms ? strtol(tick_ms, NULL, 10) : 0;
        if (n <= 0 || *end || tk <= 0 || n % tk) {   /* a bare positive integer, nothing after it */ fprintf(stderr, "[sim] reject SIM_SHOT_AFTER_MS='%s' (needs SIM_TICK_MS, a positive multiple of it)\n", a); return 2; }
        shot_after = n;
    }
    bool shot_due = false;
#endif
    while (ticks != 0) {
        lvgl_port_lock(0);
        lv_timer_handler();
#ifdef CYDSIM
        if (pump) sim_touch_pump();   /* drain tap-zone verbs under the lock */
        uint32_t lastv;
        if (shot_after > 0 && sim_touch_settled(&lastv) && lv_tick_elaps(lastv) >= (uint32_t)shot_after) {
            lv_anim_refr_now();   /* every tween evaluated at exactly its own elapsed time */
            fprintf(stderr, "[sim] shot at +%u ms\n", (unsigned)lv_tick_elaps(lastv));   /* the real elapsed time */
            shot_due = true;
        }
#endif
        lvgl_port_unlock();
#ifdef CYDSIM
        if (shot_due) break;
#endif

        usleep(5000);
        uint32_t t = now_ms();
        lv_tick_inc(tick_ms ? (uint32_t)atoi(tick_ms) : t - last);
        last = t;
        if (ticks > 0) ticks--;
    }

#ifdef CYDSIM
    if (shot_after > 0 && !shot_due) { fprintf(stderr, "[sim] SIM_SHOT_AFTER_MS: input never settled\n"); return 3; }
#endif
#if LV_USE_SNAPSHOT
    {
        const char *shot = getenv("SIM_SHOT");
        if (shot) { lvgl_port_lock(0); sim_write_ppm(shot); lvgl_port_unlock(); }
    }
#endif

    fprintf(stderr, "[sim] clean exit\n");
    return 0;
}
