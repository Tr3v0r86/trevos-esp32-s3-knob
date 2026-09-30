// trevos.c — minimal TrevOS runtime: app registry, lifecycle, input routing, tick.
//
// Deliberately bare: it owns the mechanism (mount an app on the screen, route turn /
// commit / home, pump tick), not the look. The home screen is an un-styled stub; the
// styled home and the app faces are the design agent's domain.
#include "trevos.h"
#include "trevos_theme.h"
#include "trevos_ui.h"
#include "trev_wake.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TREV_MAX_APPS 16   // the ring shows 8 of them (D5); dummies and future apps share the rest

static const char *TAG = "trevos";
static const trev_app_def_t *s_defs[TREV_MAX_APPS];
static int s_count;
static lv_obj_t *s_screen;
static trev_app_t s_active;          // .def == NULL means we are on home
static int s_home_app = -1;          // a registered app to use as home, or -1 for the stub
static int s_tap_x, s_tap_y;         // last content tap, see trev_last_tap
static void (*s_rail_init)(lv_obj_t *);
static void (*s_rail_update)(lv_obj_t *);
static uint32_t s_activity_stamp;    // lv_tick_get() at the last touch; see trev_idle_ms
static uint32_t s_dark_ms;           // see trev_set_dark_ms; 0 = never dark
static bool s_wheel_invert;
static bool s_has_wheel;
static void (*s_feedback)(trev_feedback_t);

static void clear_screen(void)
{
    if (s_active.def && s_active.def->on_stop) s_active.def->on_stop(&s_active);
    s_active.def = NULL;
    s_active.root = NULL;
    s_active.state = NULL;
    lv_obj_clean(s_screen);          // deletes every child (home stub or app root)
}

static void show_home(void)
{
    clear_screen();
    lv_obj_t *home = lv_obj_create(s_screen);
    lv_obj_remove_style_all(home);
    lv_obj_set_size(home, lv_pct(100), lv_pct(100));
    lv_obj_t *l = lv_label_create(home);
    lv_label_set_text(l, "TrevOS");
    lv_obj_set_style_text_color(l, lv_color_hex(0xF2F2F2), 0);
    lv_obj_center(l);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_active.def && s_active.def->on_tick) s_active.def->on_tick(&s_active, lv_tick_get());
}

void trev_init(lv_obj_t *screen)
{
    s_screen = screen;
    // Own an opaque background so transparent home / app roots do not show white.
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    s_active.def = NULL;
    s_active.root = NULL;
    s_activity_stamp = lv_tick_get();   // active at boot, not already idle
    show_home();
    lv_timer_create(tick_cb, 100, NULL);
    ESP_LOGI(TAG, "TrevOS up (%d apps)", s_count);
}

void trev_app_register(const trev_app_def_t *def)
{
    if (!def) return;
    if (s_count >= TREV_MAX_APPS) { ESP_LOGE(TAG, "registry full, %s dropped", def->id); return; }
    // v1 defs predate on_gesture but are still a valid frozen-contract app: accept them,
    // just flag it once so a stale app is visible in the log rather than silently dropped.
    if (def->api_version == 1) {
        ESP_LOGW(TAG, "%s registered at API v1 (on_gesture unavailable); consider v2", def->id);
    } else if (def->api_version != 2 && def->api_version != TREV_APP_API_VERSION) {
        return;
    }
    s_defs[s_count++] = def;
}

int trev_app_count(void) { return s_count; }

const trev_app_def_t *trev_app_def(int index)
{
    return (index >= 0 && index < s_count) ? s_defs[index] : NULL;
}

void trev_set_home_app(int index) { s_home_app = (index >= 0 && index < s_count) ? index : -1; }

#if TT_ROUND_DISPLAY
// The open sweep: one full-circle rim arc in the arriving app's accent, drawn over the
// incoming face and gone by the time it settles. Round-only: a rectangular board has no
// rim to sweep, so this whole block (and its call in trev_open) compiles out there.
//
// Accent lookup is a tiny id-keyed table instead of a field on trev_app_def_t: the contract is
// TREV_APP_API_VERSION 2 (v1 still accepted as legacy, see trev_app_register above) and every
// field added to it is a version bump every app pays for, while an app's own theme header must
// not leak into the OS core either. Hex literals with provenance comments beat both.
static lv_color_t sweep_accent(const char *id)
{
    // id is "pomodist" (not "pomodoist"), the app's own registered id, typo and all;
    // see boards/tdisplay-s3/main/main.c's POMODIST def. Matching the real string, not the
    // app's display name, is the whole point of keying off id.
    if (id && !strcmp(id, "pomodist")) return TT_CORAL;
    return TT_SLATE;                                                   // home / default
}

static void sweep_exec(void *var, int32_t v) { lv_arc_set_value((lv_obj_t *)var, v); }
static void sweep_done(lv_anim_t *a) { lv_obj_del((lv_obj_t *)a->var); }

// Drawn as a screen-level sibling BEFORE the app root is populated, so it starts animating
// while on_start is still building content; foregrounded so it stays the topmost layer once
// that content mounts, rather than being buried under it as the next-created sibling.
static void open_sweep(const char *app_id)
{
    lv_obj_t *arc = lv_arc_create(s_screen);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, lv_pct(96), lv_pct(96));
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 360);   // default 0..100 clamps the 0..360 value ~30ms in
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);   // track: invisible
    lv_arc_set_angles(arc, 0, 0);
    lv_obj_set_style_arc_color(arc, sweep_accent(app_id), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(arc);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 320);
    lv_anim_set_exec_cb(&a, sweep_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, sweep_done);   // deletes the arc; no residue once it settles
    lv_anim_start(&a);
}
#endif

void trev_open(int index)
{
    if (index < 0 || index >= s_count) return;
    clear_screen();
    tt_actionbar_mode(3);   // every open starts on thirds; a face that draws a two-verb bar selects its own mode
    s_active.def = s_defs[index];
    s_active.root = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_active.root);
    lv_obj_set_size(s_active.root, lv_pct(100), lv_pct(100));
#if TT_ROUND_DISPLAY
    open_sweep(s_active.def->id);   // before on_start's content mounts, so it sweeps over it
#endif
    if (s_active.def->on_start) s_active.def->on_start(&s_active, s_active.root);
    ESP_LOGI(TAG, "open %s", s_active.def->id);
    trev_feedback(TREV_FB_OPEN);
}

void trev_input_turn(trev_turn_t dir)
{
    trev_input_note_activity();
    if (s_active.def && s_active.def->on_turn) s_active.def->on_turn(&s_active, dir);
}

void trev_input_commit(void)
{
    if (s_active.def && s_active.def->on_commit) {
        trev_feedback(TREV_FB_COMMIT);   // before on_commit, so a commit that opens an app feels COMMIT then OPEN
        s_active.def->on_commit(&s_active);
    }
}

bool trev_input_wheel(int detent)
{
    int dir = trev_wheel_dir(detent, s_wheel_invert);
    if (!dir) return false;
    // Decide first: noting activity resets the idle age the dark test reads.
    trev_wheel_act_t act = trev_wheel_action(trev_idle_ms(), s_dark_ms);
    trev_input_note_activity();
    if (act == TREV_WHEEL_WAKE) {
        ESP_LOGI(TAG, "wheel: wake only");
        return false;
    }
    trev_turn_t t = dir > 0 ? TREV_TURN_NEXT : TREV_TURN_PREV;
    if (s_active.def && s_active.def->on_wheel) return s_active.def->on_wheel(&s_active, t);
    trev_input_turn(t);
    return true;
}

void trev_set_wheel_invert(bool invert) { s_wheel_invert = invert; }
void trev_set_has_wheel(bool has) { s_has_wheel = has; }
bool trev_has_wheel(void) { return s_has_wheel; }
void trev_set_dark_ms(uint32_t ms) { s_dark_ms = ms; }
uint32_t trev_dark_ms(void) { return s_dark_ms; }
void trev_set_feedback(void (*fn)(trev_feedback_t)) { s_feedback = fn; }
void trev_feedback(trev_feedback_t fb) { if (s_feedback) s_feedback(fb); }

void trev_input_home(void) { if (s_home_app >= 0) trev_open(s_home_app); else show_home(); }

void trev_input_gesture(trev_gesture_t g)
{
    if (s_active.def && s_active.def->on_gesture) {
        s_active.def->on_gesture(&s_active, g);
        return;
    }
    // No app-level handler: give a swipe a basic meaning (page next/prev) instead of
    // dropping it outright. Every other gesture (up/down swipe, face down/up, content tap)
    // is dropped silently until an app cares. A content tap in particular has no sensible
    // default: what a tap on the middle of a face means is entirely the face's business.
    if (g == TREV_GESTURE_SWIPE_LEFT)  trev_input_turn(TREV_TURN_NEXT);
    else if (g == TREV_GESTURE_SWIPE_RIGHT) trev_input_turn(TREV_TURN_PREV);
}

void trev_input_content_tap(int x, int y)
{
    s_tap_x = x; s_tap_y = y;
    trev_input_gesture(TREV_GESTURE_TAP_CONTENT);
}

void trev_last_tap(int *x, int *y)
{
    if (x) *x = s_tap_x;
    if (y) *y = s_tap_y;
}

void trev_set_rail(void (*init)(lv_obj_t *), void (*update)(lv_obj_t *)) { s_rail_init = init; s_rail_update = update; }
void trev_rail_init(lv_obj_t *l) { if (s_rail_init) s_rail_init(l); }
void trev_rail_update(lv_obj_t *l)
{
    if (s_rail_update) { s_rail_update(l); return; }
    if (!l) return;
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char buf[24];
    if (tm.tm_year + 1900 < 2020) snprintf(buf, sizeof buf, "--:--");
    else snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    if (strcmp(lv_label_get_text(l), buf) != 0) lv_label_set_text(l, buf);
}

void trev_input_note_activity(void) { s_activity_stamp = lv_tick_get(); }

uint32_t trev_idle_ms(void) { return lv_tick_elaps(s_activity_stamp); }

bool trev_has_direct_touch(void)
{
    return s_active.def && s_active.def->api_version>=3 && s_active.def->on_drag;
}
void trev_input_drag(int dx,int dy)
{
    if(trev_has_direct_touch()) s_active.def->on_drag(&s_active,dx,dy);
}
