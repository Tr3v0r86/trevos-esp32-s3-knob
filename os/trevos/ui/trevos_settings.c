// trevos_settings.c - the Settings face (P2 layout, P3 rows). Layout is the approved mockup
// (settings-list.dc.html, settings-value.dc.html, settings-confirm.dc.html): a vertical list whose
// selected row is always the slate pill at y=180, five slots visible at a 56 px pitch, the outer
// rows narrowed to follow the circle. The pill is a fixed object and the rows travel through it on
// one animated list offset (Task 5.12.4): every row's y, width, inset and text colour is a function
// of its distance from the pill, so a detent glides and a rest frame is the old layout exactly. What the list shows is a trev_setting_row_t array the board
// binds (trev_settings_bind); with none bound, a default set (About, Chip, Free heap, Uptime,
// Restart) stands in. The wheel moves the selection, or scrubs an open VALUE row (D14); a tap on
// the pill acts (VALUE opens/closes, TOGGLE flips, ACTION runs, CONFIRM is D15's two-tap); a tap
// on a neighbour selects it. The face never persists: see trev_settings_set_idle_cb.
//
#include "trevos_theme.h"
#if TT_ROUND_DISPLAY

#include "trevos.h"
#include "trevos_ui.h"
#include "trevos_settings.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VISIBLE 5                 // rows visible at rest: -2..+2 around the pill
#define SLOTS 7                   // row slots: the five plus one each side for rows crossing the clip
#define PITCH 56                  // one row
#define PILL_CY 180
#define PILL_W 280
#define PILL_H 48
#define ROW_H 32
#define CLIP_Y 52                 // the list clips to y 52..308 (below the rail, above the bezel)
#define CLIP_H 256
#define UNBOUND (-1000)
#define CROSS_MS 180              // a detent that crosses the ADVANCED eyebrow travels two pitches
#define CONFIRM_MS 3000           // D15
#define SAVE_MS 1000              // D14: idle time after the last change before the save hook
#define MAXR 32                   // rows a board may bind; ponytail: more are ignored (log says so)
#define ARC_D 360                 // the rim arc: r 168..180, 12 px wide
#define ARC_W 12

static const char *TAG = "trevos";

// Slot table from the mockup markup, by distance u from the pill's centre: the pill row is 280 wide
// at x40 (pad 20), the neighbours 264 at x48 (pad 20), the next 232 at x64 (pad 16), then 200 at x80.
// A row between two entries interpolates linearly; at rest (u = 0, 56, 112) it is the mockup's.
static const int16_t TBL_X[4] = { 40, 48, 64, 80 }, TBL_W[4] = { 280, 264, 232, 200 }, TBL_PAD[4] = { 20, 20, 16, 16 };

// ---- the default row set (what P2's placeholder showed) ----
static void t_about(char *b, size_t n) { snprintf(b, n, "%s", esp_app_get_description()->version); }
static void t_chip(char *b, size_t n)  { snprintf(b, n, "%s", CONFIG_IDF_TARGET); }
static void t_heap(char *b, size_t n)  { snprintf(b, n, "%u KB", (unsigned)(esp_get_free_heap_size() / 1024)); }
static void t_up(char *b, size_t n)
{
    unsigned s = (unsigned)(esp_timer_get_time() / 1000000);
    if (s < 60) snprintf(b, n, "%us", s);
    else if (s < 3600) snprintf(b, n, "%um %02us", s / 60, s % 60);
    else snprintf(b, n, "%uh %02um", s / 3600, (s / 60) % 60);
}
static void a_restart(void) { ESP_LOGI(TAG, "settings: restart"); esp_restart(); }

static const trev_setting_row_t DEFAULT_ROWS[] = {
    { .label = "About",     .kind = TREV_ROW_INFO,    .text = t_about },
    { .label = "Chip",      .kind = TREV_ROW_INFO,    .text = t_chip },
    { .label = "Free heap", .kind = TREV_ROW_INFO,    .text = t_heap },
    { .label = "Uptime",    .kind = TREV_ROW_INFO,    .text = t_up },
    { .label = "Restart",   .kind = TREV_ROW_CONFIRM, .act = a_restart },
};

// ---- state ----
// vsrc = the value text as update_values() last set it. LVGL writes the dots of LONG_DOT into the
// label's own text, so the label can never be compared against a fresh value; vsrc can.
// d = the display index the slot is bound to (slot of d is s_slot[d mod SLOTS], so a slot rebinds
// only when a row enters the window); w, pad = the geometry fit_slot last measured against.
typedef struct { lv_obj_t *box, *lab, *val; char vsrc[48]; int d, w, pad; } slot_t;
static slot_t s_slot[SLOTS];
static lv_obj_t *s_root, *s_clock, *s_arc, *s_bar, *s_pill, *s_list;
static int s_off;                          // the list offset in px the rows are placed at (56 x the index at the pill)
static const trev_setting_row_t *s_rows;   // NULL = DEFAULT_ROWS
static int s_n;
static int8_t s_disp[MAXR + 1];            // display order: row index, or -1 for the ADVANCED eyebrow
static int s_nd;
static int s_sel;                          // index into s_disp, never the eyebrow
static bool s_open, s_armed, s_pending;
static lv_timer_t *s_revert, *s_idle;
static void (*s_idle_cb)(void);

#define ROW_AT(d) (&s_rows[s_disp[d]])
#define CUR ROW_AT(s_sel)

static bool built(void) { return s_clock != NULL; }

// Rows -> display order: an ADVANCED eyebrow goes before the first group-1 row, and is the
// only non-selectable entry. Selection lands on the first real row.
static void layout(void)
{
    if (!s_rows || s_n <= 0) { s_rows = DEFAULT_ROWS; s_n = (int)(sizeof DEFAULT_ROWS / sizeof DEFAULT_ROWS[0]); }
    if (s_n > MAXR) { ESP_LOGW(TAG, "[settings] %d rows bound, showing %d", s_n, MAXR); s_n = MAXR; }
    s_nd = 0;
    bool eyebrow = false;
    for (int i = 0; i < s_n; i++) {
        if (!eyebrow && s_rows[i].group) { s_disp[s_nd++] = -1; eyebrow = true; }
        s_disp[s_nd++] = (int8_t)i;
    }
    s_sel = s_disp[0] < 0 ? 1 : 0;
}

static void log_rows(void)
{
    for (int i = 0; i < s_n; i++) ESP_LOGI(TAG, "[settings] row %d %s", i, s_rows[i].label);
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static void row_text(const trev_setting_row_t *r, char *buf, size_t n)
{
    buf[0] = '\0';
    if (r->text) r->text(buf, n);
    else if (r->kind == TREV_ROW_VALUE && r->get) snprintf(buf, n, "%d", r->get());
    else if (r->kind == TREV_ROW_TOGGLE && r->get) snprintf(buf, n, "%s", r->get() ? "On" : "Off");
}

// The mockup's rail is a bare HH:MM, so this does not use the board's registered rail (the
// disk's carries sync and pending marks). --:-- until a clock source has landed.
static void set_clock(void)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[24];   // 24 for -Wformat-truncation, as in trevos_home_round.c
    if (tm.tm_year + 1900 < 2020) snprintf(buf, sizeof buf, "--:--");
    else snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    set_text(s_clock, buf);
}

// Label fit: a label never collides with its value and neither runs off the pill. The label is
// dotted (LONG_DOT) into the row's inner width minus the value's measured width minus a gap; a
// value so long it would leave the label under LAB_MIN is dotted itself. Fonts do not shrink.
// The width is the slot's CURRENT width (a travelling row narrows and widens), so place() fits a
// slot whenever its width or inset changes. Values move (heap, uptime, TAP AGAIN), so
// update_values() fits after every text change, and refresh() does too. It measures vsrc, never
// the label's text, which may already be dotted.
#define FIT_GAP 8
#define LAB_MIN 48
// Each setter invalidates the object, so only touch a label whose long mode or size actually
// changed. update_values() runs this only when a value changed or refresh() asks, not every tick.
static void fit_size(lv_obj_t *o, lv_label_long_mode_t mode, int32_t w, int32_t h)
{
    if (lv_label_get_long_mode(o) != mode) lv_label_set_long_mode(o, mode);
    if (lv_obj_get_style_width(o, 0) != w || lv_obj_get_style_height(o, 0) != h) lv_obj_set_size(o, w, h);
}

static bool is_row(const slot_t *s) { return s->d >= 0 && s->d < s_nd && s_disp[s->d] >= 0; }
static slot_t *slot_of(int d) { return &s_slot[((d % SLOTS) + SLOTS) % SLOTS]; }

static void fit_slot(slot_t *s)
{
    if (!is_row(s) || s->w < 0) return;   // the eyebrow is content-sized; an unplaced slot has no width yet
    int inner = s->w - 2 * s->pad;
    const char *vt = s->vsrc;
    int vw = 0;
    if (vt[0]) {
        lv_point_t sz;
        lv_text_get_size(&sz, vt, TT_F_LABEL, s_armed && s->d == s_sel ? 1 : 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        vw = sz.x;
        int vmax = inner - LAB_MIN - FIT_GAP;
        if (vw > vmax) {
            vw = vmax;
            fit_size(s->val, LV_LABEL_LONG_DOT, vw, lv_font_get_line_height(TT_F_LABEL));   // dots need a fixed one-line height
        } else {
            fit_size(s->val, lv_label_get_long_mode(s->val), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        }
        vw += FIT_GAP;
    }
    fit_size(s->lab, LV_LABEL_LONG_DOT, inner - vw, lv_font_get_line_height(TT_F_TILE));
}

static void fit_labels(void)
{
    for (int i = 0; i < SLOTS; i++) fit_slot(&s_slot[i]);
}

// One slot's value text: TAP AGAIN on the armed pill row, else the row's own. True when it changed.
static bool sync_value(slot_t *s)
{
    if (!is_row(s)) return false;
    char v[48];
    if (s->d == s_sel && s_armed) snprintf(v, sizeof v, "TAP AGAIN");
    else row_text(&s_rows[s_disp[s->d]], v, sizeof v);
    if (strcmp(s->vsrc, v) == 0) return false;
    snprintf(s->vsrc, sizeof s->vsrc, "%s", v);
    lv_label_set_text(s->val, s->vsrc);
    return true;
}

// Value text for every slot. Runs every 100 ms tick, so it re-sets and re-fits only on a real
// change; `fit` forces the fit.
static void update_values(bool fit)
{
    for (int i = 0; i < SLOTS; i++) if (sync_value(&s_slot[i])) fit = true;
    if (fit) fit_labels();
}

// Bind a slot to display index d: everything that depends on the row and not on where it is.
// Geometry and colour are place()'s.
static void bind_slot(slot_t *s, int d)
{
    s->d = d;
    s->w = s->pad = -1;
    s->vsrc[0] = '\0';
    set_text(s->val, "");
    if (d < 0 || d >= s_nd) return;   // outside the list: place() hides it
    if (s_disp[d] < 0) {   // the ADVANCED eyebrow: a label, never a pill
        lv_obj_set_style_text_font(s->lab, TT_F_LABEL, 0);
        lv_obj_set_style_text_letter_space(s->lab, TT_TRACK_WIDE, 0);
        lv_obj_set_size(s->lab, LV_SIZE_CONTENT, LV_SIZE_CONTENT);   // fit_slot skips this slot
        set_text(s->lab, "ADVANCED");
        return;
    }
    lv_obj_set_style_text_font(s->lab, TT_F_TILE, 0);
    lv_obj_set_style_text_letter_space(s->lab, 0, 0);
    lv_obj_set_style_text_letter_space(s->val, s_armed && d == s_sel ? 1 : 0, 0);
    set_text(s->lab, s_rows[s_disp[d]].label);
    sync_value(s);
}

static int lerp_tbl(int u, const int16_t *t)
{
    if (u >= 3 * PITCH) return t[3];
    int k = u / PITCH;
    return t[k] + (t[k + 1] - t[k]) * (u - k * PITCH) / PITCH;
}

// Paper on the pill, `far` from one pitch out: the colour follows the row's distance from the pill.
static lv_color_t by_pos(int u, lv_color_t far)
{
    return u >= PITCH ? far : lv_color_mix(far, TT_PAPER, (uint8_t)(255 * u / PITCH));
}

// Set an object's x, y or width only when it changed (each setter invalidates).
static void set_pos_w(lv_obj_t *o, int x, int y, int w)
{
    if (lv_obj_get_style_x(o, 0) != x) lv_obj_set_x(o, x);
    if (lv_obj_get_style_y(o, 0) != y) lv_obj_set_y(o, y);
    if (lv_obj_get_style_width(o, 0) != w) lv_obj_set_width(o, w);
}

static void set_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), c)) lv_obj_set_style_text_color(o, c, 0);
}

// Lay every row out for list offset `off`: row d sits at cy = 180 + 56 d - off, and its x, width,
// inset and colour follow its distance u from the pill. A row wholly outside the clip is hidden.
// At off = 56 x s_sel this is the mockup's five-slot layout exactly.
static void place(int off)
{
    int f = off / PITCH;   // off is never negative: the selection is an index
    for (int k = 0; k < SLOTS; k++) {
        slot_t *s = slot_of(f - 3 + k);
        if (s->d != f - 3 + k) bind_slot(s, f - 3 + k);
    }
    for (int k = 0; k < SLOTS; k++) {
        int d = f - 3 + k;
        slot_t *s = slot_of(d);
        int cy = PILL_CY + PITCH * d - off;
        bool vis = d >= 0 && d < s_nd && cy + ROW_H / 2 > CLIP_Y && cy - ROW_H / 2 < CLIP_Y + CLIP_H;
        if (lv_obj_has_flag(s->box, LV_OBJ_FLAG_HIDDEN) == vis) {
            if (vis) lv_obj_remove_flag(s->box, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s->box, LV_OBJ_FLAG_HIDDEN);
        }
        if (!vis) continue;
        int u = cy > PILL_CY ? cy - PILL_CY : PILL_CY - cy;
        int w = lerp_tbl(u, TBL_W), pad = lerp_tbl(u, TBL_PAD);
        set_pos_w(s->box, lerp_tbl(u, TBL_X), cy - ROW_H / 2 - CLIP_Y, w);
        if (pad != s->pad) {
            lv_obj_align(s->lab, LV_ALIGN_LEFT_MID, pad, 0);
            lv_obj_align(s->val, LV_ALIGN_RIGHT_MID, -pad, 0);
        }
        set_color(s->lab, by_pos(u, s_disp[d] < 0 ? TT_DESC : TT_INK));
        set_color(s->val, by_pos(u, TT_DESC));
        if (w != s->w || pad != s->pad) { s->w = w; s->pad = pad; fit_slot(s); }
    }
}

// The list offset is the one animated scalar (tt_step's exec).
static void off_exec(void *var, int32_t v) { (void)var; s_off = v; place(v); }

// The glide has landed. tt_step's settle hook: pick up any value the moving rows did not show. It is
// also the sim's `[motion] settle` line, which settings_wifi counts its idle flushes from.
static void list_settled(void *var) { (void)var; update_values(false); }

// Land the list on the selection at once (a tap, a bind, a build): any running glide is dropped.
static void land(void) { tt_step(s_list, off_exec, 0, PITCH * s_sel, 0, NULL); }

// Restyle for the selection, arm and open state: the pill's colour and the armed value's tracking.
// Instant by design (D16): a colour swap, no tween, so it is identical with and without
// TT_NO_LAYER_FX. The pill is slate; it flips to ink while armed (D15) or while its VALUE row is
// open (D14). The rows are placed where the list offset has them, mid-glide or at rest.
static void refresh(void)
{
    lv_obj_set_style_bg_color(s_pill, s_armed || s_open ? TT_INK : TT_SLATE, 0);
    for (int i = 0; i < SLOTS; i++) {
        slot_t *s = &s_slot[i];
        if (is_row(s)) lv_obj_set_style_text_letter_space(s->val, s_armed && s->d == s_sel ? 1 : 0, 0);
    }
    place(s_off);
    update_values(true);
}

// ---- the deferred save (D14): the face only tells the board when a change has settled ----
static void flush_idle(void)
{
    if (s_idle) { lv_timer_delete(s_idle); s_idle = NULL; }
    if (s_pending) { s_pending = false; if (s_idle_cb) s_idle_cb(); }
}

static void idle_cb(lv_timer_t *t)
{
    (void)t;
    s_idle = NULL;                   // one-shot: the repeat count deletes the timer after this
    flush_idle();
}

static void changed(const trev_setting_row_t *r)
{
    s_pending = true;
    if (s_idle) lv_timer_reset(s_idle);
    else { s_idle = lv_timer_create(idle_cb, SAVE_MS, NULL); lv_timer_set_repeat_count(s_idle, 1); }
    ESP_LOGI(TAG, "[settings] set %s %d", r->label, r->get ? r->get() : 0);
}

// ---- D15 arm / disarm ----
static void disarm(void)
{
    if (s_revert) { lv_timer_delete(s_revert); s_revert = NULL; }
    if (s_armed) { s_armed = false; if (built()) refresh(); }
}

static void revert_cb(lv_timer_t *t)
{
    (void)t;
    s_revert = NULL;                 // one-shot: the repeat count deletes the timer after this
    disarm();
}

// ---- D14 open VALUE row ----
static int arc_pos(const trev_setting_row_t *r)
{
    if (r->max <= r->min || !r->get) return 0;
    int v = r->get();
    if (v < r->min) v = r->min;
    if (v > r->max) v = r->max;
    return (v - r->min) * 1000 / (r->max - r->min);
}

static void close_row(void)
{
    if (!s_open) return;
    flush_idle();
    if (s_arc) { lv_obj_delete(s_arc); s_arc = NULL; }
    if (s_bar) { lv_obj_delete(s_bar); s_bar = NULL; }
    s_open = false;
    if (built()) refresh();
}

static void open_row(const trev_setting_row_t *r)
{
    s_open = true;
    ESP_LOGI(TAG, "[settings] open %s", r->label);
    if (trev_has_wheel()) {
        // The mockup's rim: track + ink indicator, 12 px at r 168..180, from 12 o'clock clockwise.
        s_arc = lv_arc_create(s_root);
        lv_obj_set_size(s_arc, ARC_D, ARC_D);
        lv_obj_center(s_arc);
        lv_arc_set_rotation(s_arc, 270);
        lv_arc_set_bg_angles(s_arc, 0, 360);
        lv_arc_set_range(s_arc, 0, 1000);
        lv_arc_set_value(s_arc, 0);
        lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(s_arc, ARC_W, LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_arc, TT_RING_TRACK, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_arc, ARC_W, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s_arc, TT_INK, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_INDICATOR);
        lv_obj_move_to_index(s_arc, 0);   // behind the rail and the pills
        tt_arc_anim(s_arc, arc_pos(r), 600);
    } else {
        // No wheel: - and + scrub, DONE closes. The zones reach settings_on_gesture (below).
        s_bar = tt_actionbar(s_root, "-", "DONE", "+", tt_skin_paper());
    }
    refresh();
}

// One detent or bar step. Open VALUE row: scrub by `step`, live via `set`; true only when the
// value moved (false at min/max, D6). Otherwise move the selection past the eyebrow; true only
// when it moved.
static bool step_by(int dir, bool wheel)
{
    if (s_open) {
        const trev_setting_row_t *r = CUR;
        int st = r->step > 0 ? r->step : 1;
        int v = r->get ? r->get() : 0;
        int nv = v + dir * st;
        if (nv < r->min) nv = r->min;
        if (nv > r->max) nv = r->max;
        if (nv == v || !r->set) return false;
        r->set(nv);
        changed(r);
        if (s_arc) {
            if (wheel) tt_arc_step(s_arc, arc_pos(r));   // 150 ms from the live angle, replacing any running tween
            else tt_arc_anim(s_arc, arc_pos(r), 600);
        }
        update_values(false);
        return true;
    }
    int d = s_sel + dir;
    while (d >= 0 && d < s_nd && s_disp[d] < 0) d += dir;
    if (d < 0 || d >= s_nd) return false;   // ends of the list do not wrap
    disarm();
    int prev = s_sel;
    s_sel = d;
    ESP_LOGI(TAG, "[settings] sel %s", CUR->label);
    // The list glides from where it is to the new row; the detent that crosses the ADVANCED eyebrow
    // (two pitches) takes the longer band. No refresh: the pill does not move and the rows follow the offset.
    int from = tt_step_live(s_list, off_exec, PITCH * prev), to = PITCH * d;
    tt_step(s_list, off_exec, from, to, abs(to - PITCH * prev) > PITCH ? CROSS_MS : TT_STEP_MS, list_settled);
    return true;
}

static void act(void)
{
    const trev_setting_row_t *r = CUR;
    switch (r->kind) {
    case TREV_ROW_VALUE:
        if (s_open) close_row(); else open_row(r);
        break;
    case TREV_ROW_TOGGLE:
        if (r->get && r->set) { r->set(!r->get()); changed(r); refresh(); }
        break;
    case TREV_ROW_ACTION:
        if (r->act) r->act();
        break;
    case TREV_ROW_CONFIRM:
        if (!s_armed) {
            s_armed = true;
            ESP_LOGI(TAG, "[settings] arm %s", r->label);
            s_revert = lv_timer_create(revert_cb, CONFIRM_MS, NULL);
            lv_timer_set_repeat_count(s_revert, 1);
            refresh();
            break;
        }
        disarm();
        ESP_LOGI(TAG, "settings: confirm %s", r->label);
        trev_feedback(TREV_FB_COMMIT);   // D15: the confirming tap thunks, then acts
        if (r->act) r->act();
        break;
    case TREV_ROW_INFO:
        break;                       // selectable (the mockup selects rows), nothing to do
    }
}

// Selecting a neighbour by tap. The eyebrow is not selectable.
static void select_slot(int k)
{
    int d = s_sel + k;
    if (s_open || d < 0 || d >= s_nd || s_disp[d] < 0) return;
    disarm();
    s_sel = d;
    ESP_LOGI(TAG, "[settings] sel %s", CUR->label);
    land();
}

void trev_settings_bind(const trev_setting_row_t *rows, int n)
{
    if (built()) { close_row(); disarm(); }   // both refresh() against the OLD table: swap after
    s_rows = rows;
    s_n = n;
    layout();
    if (built()) {
        log_rows();
        for (int i = 0; i < SLOTS; i++) s_slot[i].d = UNBOUND;   // the rows changed under every slot
        land();
        refresh();
    }
}

void trev_settings_set_idle_cb(void (*fn)(void)) { s_idle_cb = fn; }

// A row slot: a transparent box (text only) inside the clip container. place() gives it its geometry.
static void build_slot(lv_obj_t *list, slot_t *s)
{
    lv_obj_t *b = lv_obj_create(list);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, 0, ROW_H);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    s->box = b;
    s->lab = tt_label(b, "", TT_F_TILE, TT_INK, 0);
    s->val = tt_label(b, "", TT_F_LABEL, TT_DESC, 0);
    s->vsrc[0] = '\0';
    s->d = UNBOUND;
    s->w = s->pad = -1;
}

static void settings_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(root, sk);
    s_root = root;
    s_open = s_armed = false;
    layout();                        // rows (or the default set) -> display order, selection on the first

    // The rail is the mockup's 152x20 box at (104,30): SETTINGS left, the clock right, no
    // padding, tracking 1 on both. tt_statusbar draws the bar chord-wide with TT_PAD, so it is
    // resized and its two labels re-aligned here.
    lv_obj_t *bar = tt_statusbar(root, "SETTINGS", sk, &s_clock);
    lv_obj_set_size(bar, 152, 20);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 104, 30);
    lv_obj_align(lv_obj_get_child(bar, 0), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_letter_space(s_clock, TT_TRACK_WIDE, 0);
    set_clock();

    // The pill is a fixed object under the list; the list clips the travelling rows to y 52..308.
    s_pill = lv_obj_create(root);
    lv_obj_remove_style_all(s_pill);
    lv_obj_clear_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_pill, PILL_W, PILL_H);
    lv_obj_set_pos(s_pill, 40, PILL_CY - PILL_H / 2);
    lv_obj_set_style_radius(s_pill, PILL_H / 2, 0);   // 24, as mocked
    lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
    s_list = lv_obj_create(root);
    lv_obj_remove_style_all(s_list);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_list, 360, CLIP_H);
    lv_obj_set_pos(s_list, 0, CLIP_Y);
    for (int i = 0; i < SLOTS; i++) build_slot(s_list, &s_slot[i]);
    s_off = PITCH * s_sel;
    log_rows();
    refresh();

    tt_face_enter(root, s_pill, NULL, 0);
}

static void settings_on_stop(trev_app_t *app)
{
    (void)app;
    flush_idle();                    // a change still inside its 1 s window is saved now
    if (s_revert) { lv_timer_delete(s_revert); s_revert = NULL; }
    s_armed = s_open = false;
    s_arc = s_bar = s_root = s_pill = s_list = NULL;   // the widgets go with the screen's clean
    s_clock = NULL;
    memset(s_slot, 0, sizeof s_slot);
}

// Facts that move (heap, uptime) are re-read on the tick; update_values() compares them against
// each slot's vsrc and only touches a label whose value changed (set_text() does the same for the clock).
static void settings_on_tick(trev_app_t *app, uint32_t now_ms)
{
    (void)app; (void)now_ms;
    if (!built()) return;
    set_clock();
    update_values(false);
}

// Bar zones (- and +) on a board without a wheel, and the OS fallback for a swipe.
static void settings_on_turn(trev_app_t *app, trev_turn_t dir)
{
    (void)app;
    step_by(dir == TREV_TURN_NEXT ? 1 : -1, false);
}

// A detent: felt only when the selection or the value actually moved (D6).
static bool settings_on_wheel(trev_app_t *app, trev_turn_t dir)
{
    (void)app;
    return step_by(dir == TREV_TURN_NEXT ? 1 : -1, true);
}

static void settings_on_commit(trev_app_t *app) { (void)app; act(); }

// Every release arrives as a content tap (the no-op on_drag below, same as the ring). A tap
// on the pill acts; a tap on a neighbour selects it; anywhere else is inert.
static void settings_on_gesture(trev_app_t *app, trev_gesture_t g)
{
    (void)app;
    if (g != TREV_GESTURE_TAP_CONTENT) return;
    int x, y;
    trev_last_tap(&x, &y);
    // A no-op on_drag makes every release a content tap, so the disk's bar zones (- DONE +) land
    // here instead of on_turn/on_commit: hit-test them with the same numbers the bar drew.
    if (s_bar && y >= tt_actionbar_top()) {
        int z = tt_zone_at(x);
        if (z < 0) step_by(-1, false); else if (z > 0) step_by(1, false); else close_row();
        return;
    }
    int k = (y - PILL_CY + (y >= PILL_CY ? PITCH / 2 : -PITCH / 2)) / PITCH;   // nearest rest slot, -2..+2
    if (k < -VISIBLE / 2 || k > VISIBLE / 2) return;
    if (k == 0) act(); else select_slot(k);
}

static void settings_on_drag(trev_app_t *app, int dx, int dy) { (void)app; (void)dx; (void)dy; }

const trev_app_def_t TREV_SETTINGS = {
    .api_version = TREV_APP_API_VERSION,
    .id = "settings", .name = "Settings",
    .on_start = settings_on_start, .on_stop = settings_on_stop, .on_tick = settings_on_tick,
    .on_turn = settings_on_turn, .on_wheel = settings_on_wheel, .on_commit = settings_on_commit,
    .on_gesture = settings_on_gesture, .on_drag = settings_on_drag,
};

#endif // TT_ROUND_DISPLAY
