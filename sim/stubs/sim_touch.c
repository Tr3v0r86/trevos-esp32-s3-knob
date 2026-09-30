/* sim/stubs/sim_touch.c — host touch indev for cydsim. See sim_touch.h.
 *
 * Two shared, mutex-guarded queues filled by the stdin thread and drained on the
 * LVGL/main thread:
 *   - a pointer-step queue, consumed by touch_read_cb (the SOLE LVGL caller), so a
 *     TAP persists PRESSED for >=1 read then RELEASED on a later read (LVGL never
 *     sees a one-shot press collapsed into a single invocation).
 *   - a verb queue, consumed by sim_touch_pump under the lvgl_port lock.
 * The stdin thread NEVER calls LVGL or trev_*, and NEVER takes lvgl_port_lock — only
 * its own mutex. This is the same cross-thread contract sim_buttons.c documents.
 */
#include "sim_touch.h"
#include "trevos.h"
#include "trevos_ui.h"
#include "lvgl.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#if defined(SIM_WHEEL) && defined(BSP_HAS_HAPTIC)
#include "bsp_haptic.h"   /* the puck's header (pucksim adds boards/puck/main to its -I path) */
#endif

/* V_DETAIL (D18) is the content tap the disk's bsp_touch.c synthesises for a press above the
 * action bar. The sim has no bar-relative pointer verb path, so it is injected by name
 * instead of by coordinate: `detail` on stdin is exactly what a tap on the title does. */
typedef enum { V_PREV, V_NEXT, V_COMMIT, V_HOME, V_DETAIL, V_TAPC,
               V_SWIPE_UP, V_SWIPE_DOWN, V_SWIPE_LEFT, V_SWIPE_RIGHT, V_DRAG,
               V_TAP   /* a short release; pump resolves it to V_TAPC or a zone verb, see there */
#ifdef SIM_WHEEL
               , V_TURN   /* x = +1 / -1 */
#endif
               , V_WAIT   /* x = ms of LVGL time to hold the queue head; pacing, never input */
               , V_PT     /* a pointer step (x, y, pressed) queued behind a WAIT, see pt_emit */
             } verb_t;
typedef struct { verb_t v; int x, y; bool pressed; } vq_t;

#define PQ 128
#define VQ 64

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/* pointer-step ring (consumed by read_cb); s_cur holds the last step when empty */
typedef struct { int x, y; bool pressed; } pt_t;
static pt_t s_pq[PQ];
static int s_pq_head, s_pq_tail;
static pt_t s_cur;

/* verb ring (consumed by sim_touch_pump) */
static vq_t s_vq[VQ];
static int s_vq_head, s_vq_tail;

static int s_w = 240;   /* display width, for the startup log only */
static bool s_dark_env; /* SIM_DARK_MS set: hold verbs until the screen has gone dark */

/* WAIT pacing and the shot clock (Task 5.12.0). s_paced: a WAIT has been parsed, so every pointer
 * step after it rides the verb queue behind it (the stdin thread would otherwise hand LVGL the
 * whole script's presses at t=0, before the verbs they belong to; SLEEP gets this for free from
 * the pipe). s_ref: LVGL time the next WAIT counts from. s_last/s_applied: the last applied input
 * verb, for sim_touch_settled. s_eof: stdin closed. All guarded by s_lock. */
static bool s_paced, s_eof, s_ref_set;
static uint32_t s_ref, s_last, s_applied;

static void pq_push(int x, int y, bool pressed)
{
    int n = (s_pq_tail + 1) % PQ;
    if (n == s_pq_head) return;            /* full: drop (bounded, harmless) */
    s_pq[s_pq_tail].x = x; s_pq[s_pq_tail].y = y; s_pq[s_pq_tail].pressed = pressed;
    s_pq_tail = n;
}

static void vq_push(verb_t v, int x, int y)
{
    int n = (s_vq_tail + 1) % VQ;
    if (n == s_vq_head) { fprintf(stderr, "[touch] reject 'queue full'\n"); exit(2); }   /* a dropped verb would skew the script silently */
    s_vq[s_vq_tail].v = v; s_vq[s_vq_tail].x = x; s_vq[s_vq_tail].y = y; s_vq[s_vq_tail].pressed = false;
    s_vq_tail = n;
}

/* A pointer step: straight to the pointer ring, or behind the verb queue once a WAIT was seen. */
static void pt_emit(int x, int y, bool pressed)
{
    if (!s_paced) { pq_push(x, y, pressed); return; }
    vq_push(V_PT, x, y);
    s_vq[(s_vq_tail + VQ - 1) % VQ].pressed = pressed;
}

/* Resolved on the LVGL thread at pump time (parse() runs on the stdin thread and must never
 * call into LVGL): tt_zone_at is what the bar is drawn and hit-tested with, in whichever mode
 * the face on screen selected (thirds, halves, or one verb). */
static verb_t zone_verb(int x)
{
    int z = tt_zone_at(x);
    return z < 0 ? V_PREV : z > 0 ? V_NEXT : V_COMMIT;
}

/* ---- LVGL read callback (runs on the main/LVGL thread, under lvgl_port_lock) ---- */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    pthread_mutex_lock(&s_lock);
    if (s_pq_head != s_pq_tail) {
        s_cur = s_pq[s_pq_head];
        s_pq_head = (s_pq_head + 1) % PQ;
    }
    data->point.x = s_cur.x;
    data->point.y = s_cur.y;
    data->state = s_cur.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = (s_pq_head != s_pq_tail);   /* keep draining a TAP's steps */
    pthread_mutex_unlock(&s_lock);
}

/* ---- stdin command thread (never touches LVGL) ---- */
/* The grammar is strict (Y6, C9): a typo in a shot script must fail the shot, not silently do
 * nothing. One command per line, tokens separated by whitespace, nothing left over:
 *   TAP|TAPC|DOWN|MOVE|UP|DRAG x y     exactly two integers
 *   SWIPE UP|DOWN|LEFT|RIGHT           exactly one of the four
 *   TURN +1|-1                         only where SIM_WHEEL is set (pucksim)
 *   WAIT <ms>                          exactly one positive integer (<= 600000); pacing, see sim_touch_pump
 *   HOME | detail | QUIT               no arguments (QUIT is handled by touch_thread)
 * parse returns 1 for a queued command, 0 to reject it; touch_thread turns a 0 into
 * "[touch] reject" and exit(2). */
static bool two_ints(const char *line, int *x, int *y)
{
    char cmd[16]; int n = 0;
    return sscanf(line, "%15s %d %d %n", cmd, x, y, &n) == 3 && line[n] == 0;
}

static int parse(const char *line)
{
    char cmd[16] = "";
    if (sscanf(line, "%15s", cmd) != 1) return 0;
    int x = 0, y = 0;
    if (!strcmp(cmd, "WAIT")) {   /* exactly one positive integer, nothing after it */
        char *end; const char *a = line + 4;
        while (*a == ' ' || *a == '\t') a++;
        long ms = (*a >= '0' && *a <= '9') ? strtol(a, &end, 10) : 0;
        if (ms <= 0 || ms > 600000) return 0;
        while (*end == ' ' || *end == '\t') end++;
        if (*end) return 0;
        pthread_mutex_lock(&s_lock);
        s_paced = true;
        vq_push(V_WAIT, (int)ms, 0);
        pthread_mutex_unlock(&s_lock);
        return 1;
    }
    bool xy = !strcmp(cmd, "TAP") || !strcmp(cmd, "TAPC") || !strcmp(cmd, "DOWN") ||
              !strcmp(cmd, "MOVE") || !strcmp(cmd, "UP")   || !strcmp(cmd, "DRAG");
    if (xy && !two_ints(line, &x, &y)) return 0;

    verb_t sw = V_SWIPE_UP;
    if (!strcmp(cmd, "SWIPE")) {
        char dir[16] = ""; int n = 0;
        if (sscanf(line, "%*s %15s %n", dir, &n) != 1 || line[n] != 0) return 0;
        if      (!strcmp(dir, "UP"))    sw = V_SWIPE_UP;
        else if (!strcmp(dir, "DOWN"))  sw = V_SWIPE_DOWN;
        else if (!strcmp(dir, "LEFT"))  sw = V_SWIPE_LEFT;
        else if (!strcmp(dir, "RIGHT")) sw = V_SWIPE_RIGHT;
        else return 0;
    }

    pthread_mutex_lock(&s_lock);
    if      (!strcmp(cmd, "TAP"))  { pt_emit(x, y, true); pt_emit(x, y, false); vq_push(V_TAP, x, y); }
    else if (!strcmp(cmd, "DOWN")) { pt_emit(x, y, true); }
    else if (!strcmp(cmd, "MOVE")) { pt_emit(x, y, true); }
    else if (!strcmp(cmd, "UP"))   { pt_emit(x, y, false); }
    else if (!strcmp(cmd, "TAPC")) { pt_emit(x, y, true); pt_emit(x, y, false); vq_push(V_TAPC, x, y); }
    else if (!strcmp(cmd, "DRAG")) { vq_push(V_DRAG, x, y); }
    else if (!strcmp(line, "HOME"))   { vq_push(V_HOME, 0, 0); }
    else if (!strcmp(line, "detail")) { vq_push(V_DETAIL, 0, 0); }
    else if (!strcmp(cmd, "SWIPE"))   { vq_push(sw, 0, 0); }
#ifdef SIM_WHEEL
    else if (!strcmp(line, "TURN +1")) { vq_push(V_TURN, +1, 0); }
    else if (!strcmp(line, "TURN -1")) { vq_push(V_TURN, -1, 0); }
#endif
    else { pthread_mutex_unlock(&s_lock); return 0; }
    pthread_mutex_unlock(&s_lock);
    return 1;
}

static void *touch_thread(void *arg)
{
    (void)arg;
    char line[64];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 0) continue;
        if (!strcmp(line, "QUIT")) { fprintf(stderr, "[touch] QUIT\n"); exit(0); }
        if (!parse(line)) { fprintf(stderr, "[touch] reject '%s'\n", line); exit(2); }
        fprintf(stderr, "[touch] %s\n", line);
    }
    pthread_mutex_lock(&s_lock);
    s_eof = true;
    pthread_mutex_unlock(&s_lock);
    return NULL;
}

/* ---- public API ---- */
void sim_touch_init(lv_display_t *disp)
{
    if (disp) s_w = (int)lv_display_get_horizontal_resolution(disp);

    /* SIM_DARK_MS=<n>: the screen counts as dark after n ms idle (trev_set_dark_ms, E6).
     * Applied here because sim_main calls sim_touch_init AFTER board_app_main, i.e. after
     * trev_init and after settings_init's slp_set (the Dark after pref sets the dark threshold), so this wins. */
    {
        const char *d = getenv("SIM_DARK_MS");
        if (d) {
            char *end;
            unsigned long ms = strtoul(d, &end, 10);
            if (!*d || *end) { fprintf(stderr, "[touch] reject SIM_DARK_MS='%s'\n", d); exit(2); }
            trev_set_dark_ms((uint32_t)ms);
            s_dark_env = true;
        }
    }

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    pthread_t t;
    pthread_create(&t, NULL, touch_thread, NULL);
    pthread_detach(t);
    fprintf(stderr, "[touch] pointer indev up, w=%d"
                    " (stdin: TAP|TAPC|DOWN|MOVE|UP x y | SWIPE UP|DOWN|LEFT|RIGHT |"
                    " WAIT ms |"
#ifdef SIM_WHEEL
                    " TURN +1|-1 |"
#endif
                    " HOME | detail | QUIT)\n", s_w);
}

void sim_touch_pump(void)
{
    /* SIM_DARK_MS: the shot script means "this input arrives on a dark screen", but idle is
     * lv_tick time and the first pump runs at tick 0, so a piped TURN would beat the dark
     * threshold. Hold every verb until the screen has actually been idle past it, once. */
    if (s_dark_env) {
        static bool dark_seen;
        if (!dark_seen) {
            if (trev_idle_ms() <= trev_dark_ms()) return;
            dark_seen = true;
        }
    }
    for (;;) {
        vq_t v;
        pthread_mutex_lock(&s_lock);
        if (s_vq_head == s_vq_tail) { pthread_mutex_unlock(&s_lock); break; }
        if (!s_ref_set) { s_ref = lv_tick_get(); s_ref_set = true; }   /* a WAIT first in the script counts from the first pump */
        v = s_vq[s_vq_head];
        if (v.v == V_WAIT) {
            /* Held at the head, and everything behind it with it, until v.x ms of LVGL time have
             * passed since the previous verb was applied. It paces; it is not input, so it is not
             * an applied verb (no log line, no s_last). The next reference is the deadline itself,
             * so two WAITs in a row add up and an odd overshoot does not accumulate. */
            if (lv_tick_elaps(s_ref) < (uint32_t)v.x) { pthread_mutex_unlock(&s_lock); break; }
            s_ref += (uint32_t)v.x;
            s_vq_head = (s_vq_head + 1) % VQ;
            pthread_mutex_unlock(&s_lock);
            continue;
        }
        s_vq_head = (s_vq_head + 1) % VQ;
        s_ref = s_last = lv_tick_get(); s_applied++;
        if (v.v == V_PT) pq_push(v.x, v.y, v.pressed);   /* a paced pointer step: the indev reads it from here */
        pthread_mutex_unlock(&s_lock);
        if (v.v == V_PT) continue;
        /* A short release. On a direct-touch face (trev_has_direct_touch: api v3 with an on_drag)
         * touch_drag.h's classifier makes EVERY release a content tap (E1, `content` is true
         * whatever the y), so the sim does the same; a face without direct touch keeps the
         * bar's zone verb. Decided here, on the LVGL thread, because which face is up is only
         * known now - parse() runs on the stdin thread and must not ask. */
        if (v.v == V_TAP) v.v = trev_has_direct_touch() ? V_TAPC : zone_verb(v.x);
        switch (v.v) {
            case V_DRAG: trev_input_drag(v.x,v.y); break;
            case V_PREV:   trev_input_turn(TREV_TURN_PREV); break;
            case V_NEXT:   trev_input_turn(TREV_TURN_NEXT); break;
            case V_COMMIT: trev_input_commit(); break;
            case V_HOME:   trev_input_home(); break;
            case V_DETAIL: trev_input_gesture(TREV_GESTURE_TAP_CONTENT); break;
            case V_TAPC:   trev_input_content_tap(v.x, v.y); break;
            case V_TAP:    break;   /* resolved above */
            case V_WAIT: case V_PT: break;   /* handled above */
            case V_SWIPE_UP:    trev_input_gesture(TREV_GESTURE_SWIPE_UP); break;
            case V_SWIPE_DOWN:  trev_input_gesture(TREV_GESTURE_SWIPE_DOWN); break;
            case V_SWIPE_LEFT:  trev_input_gesture(TREV_GESTURE_SWIPE_LEFT); break;
            case V_SWIPE_RIGHT: trev_input_gesture(TREV_GESTURE_SWIPE_RIGHT); break;
#ifdef SIM_WHEEL
            /* Mirrors wheel_drain_cb in main.c (bsp_wheel.c is stubbed out, so nothing else
             * drains a detent here): a delivered turn ticks, a wake-only one does not. */
#ifdef BSP_HAS_HAPTIC
            case V_TURN:   if (trev_input_wheel(v.x)) bsp_haptic_play(HAPTIC_TICK); break;
#else
            case V_TURN:   trev_input_wheel(v.x); break;
#endif
#endif
        }
        fprintf(stderr, "[touch] verb %d applied\n", (int)v.v);
        // Publish the geometry the bar was actually DRAWN with, once. zonecheck.sh derives its
        // probe points from this line instead of carrying its own copy of the numbers: a script
        // that hardcodes them silently keeps passing after the bar moves, which is the exact
        // class of drift tt_actionbar_geom() exists to prevent. It read "67..293" against a
        // 178px bar until this line existed.
        {
            static bool once = false;
            if (!once) {
                once = true;
                int x0 = 0, bw = 0;
                tt_actionbar_geom(&x0, &bw);
                fprintf(stderr, "[touch] geom x0=%d w=%d\n", x0, bw);
            }
        }
    }
}

bool sim_touch_settled(uint32_t *last_ms)
{
    pthread_mutex_lock(&s_lock);
    bool done = s_eof && s_vq_head == s_vq_tail && s_applied > 0;
    if (done && last_ms) *last_ms = s_last;
    pthread_mutex_unlock(&s_lock);
    return done;
}
