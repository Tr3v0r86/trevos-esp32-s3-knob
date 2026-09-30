// trevos_home_ring.c - the puck's home: a ring of app segments around a disc (ADR-0020).
//
// Eight 45 degree segments on a band r=126..172, segment 0 at 12 o'clock, Settings pinned to
// 6 o'clock, the disc (r=118) naming the selection. The wheel walks the ring and a tap opens.
// Layout is the approved mockup (Main.dc.html, ring-b-cal, ring-c-settings, ring-d-flash).
//
// Everything is placed from TT_RING and tt_ring_seg_centre (trev_ring_geom.c), the same table
// tt_ring_hit hit-tests with, so the drawing and the tap zones cannot drift (D13).
//
// E12: this file exports TREV_HOME_ROUND under TT_HOME_RING=1 and is empty otherwise;
// trevos_home_round.c (the disk's pill home) is the other half of the same seam.
#include "trevos_theme.h"
#include "trevos_home.h"
#if TT_ROUND_DISPLAY && TT_HOME_RING

#include "trevos.h"
#include "trevos_ui.h"
#include "trevos_settings.h"
#include "trev_ring.h"
#include "esp_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "trevos";

#define SEG_DEG      45     // one slot of the circle
#define GAP_DEG       2     // dead air between two drawn segments (43 degrees drawn)
#define ICON_BOX     28     // icon box, 3 px stroke (D12)
#define DISC_TEXT_W 184     // the disc's text column (D12)

// One entry per segment. reg = registry index of the app it opens, -1 for an empty slot.
// ic is the icon container (a child of the root, above the sweep arc, see build_icon) and dot
// the empty-slot marker (a child of arc).
typedef struct { lv_obj_t *arc, *ic, *dot; int reg; } seg_t;

static seg_t s_seg[TT_RING_SEGS];
static int s_mask;                       // occupied segments, bit per segment
static int s_sel = -1;                   // selected segment; static so it survives leaving home (D6)
static lv_obj_t *s_sweep;                // the travelling slate arc (5.12.3), hidden at rest
static bool s_sweeping;                  // a sweep is running toward s_sel
static int32_t s_sw_target;              // its target, in unwrapped ring degrees
static bool s_sw_cov;                    // the destination icon is painted paper (the arc covers it)
static lv_obj_t *s_disc, *s_name, *s_line;
static lv_obj_t *s_st[3];                // status row: time, separator, alert (the last two shown only when alerting)
static uint32_t s_state_ms;              // when the disc's one-liner and status were last refreshed

// Hooks the board registers (trevos_home.h). The ring owns none of this state: it asks.
static void (*s_status_fn)(char *line, size_t cap);
static struct { const char *id; void (*fn)(char *buf, size_t cap); } s_line_fn[TT_RING_SEGS];
static int s_n_line_fn;
static bool (*s_haptics_on)(void);
static lv_timer_t *s_flash_t;            // the 80 ms ink flash, one at most (D18)
static int s_flash_seg;

void trev_ring_set_status(void (*fn)(char *line, size_t cap)) { s_status_fn = fn; }
void trev_ring_set_haptics_on(bool (*fn)(void)) { s_haptics_on = fn; }

void trev_ring_set_oneliner(const char *app_id, void (*fn)(char *buf, size_t cap))
{
    for (int i = 0; i < s_n_line_fn; i++)
        if (!strcmp(s_line_fn[i].id, app_id)) { s_line_fn[i].fn = fn; return; }
    if (s_n_line_fn >= TT_RING_SEGS) { ESP_LOGE(TAG, "ring: no room for a one-liner for %s", app_id); return; }
    s_line_fn[s_n_line_fn].id = app_id;      // a string literal in every caller, so it outlives the ring
    s_line_fn[s_n_line_fn++].fn = fn;
}

// ---- slots ------------------------------------------------------------------------------

// The registry is home (0), then the apps; the ring shows every app but itself. tt_ring_slots
// pins Settings to segment 4 and fills the rest clockwise from 0 around it (D5).
static void build_slots(void)
{
    bool is_settings[TT_RING_SEGS + 1];
    int reg[TT_RING_SEGS + 1];
    int8_t seg_of[TT_RING_SEGS + 1];
    int n = 0;
    for (int i = 0; i < trev_app_count() && n <= TT_RING_SEGS; i++) {
        const trev_app_def_t *d = trev_app_def(i);
        if (!d || d == &TREV_HOME_ROUND) continue;
        is_settings[n] = (d == &TREV_SETTINGS);
        reg[n++] = i;
    }
    // D5: the ring holds eight. A ninth is a build error caught on first boot, not a segment
    // silently missing from the home (the registry itself holds 16).
    if (n > TT_RING_SEGS) {
        ESP_LOGE(TAG, "ring: more than %d apps registered, the home cannot show them all", TT_RING_SEGS);
        assert(n <= TT_RING_SEGS);
    }
    s_mask = tt_ring_slots(is_settings, n, seg_of);
    for (int s = 0; s < TT_RING_SEGS; s++) s_seg[s].reg = -1;
    if (s_mask < 0) {
        ESP_LOGE(TAG, "ring: %d apps do not fit %d segments (or two Settings)", n, TT_RING_SEGS);
        s_mask = 0;
        return;
    }
    for (int k = 0; k < n; k++) s_seg[seg_of[k]].reg = reg[k];
}

// ---- icons (28 px box, 3 px rounded strokes) ---------------------------------------------
//
// Drawn as the boards draw them (SVG in a 28 px box). lv_line points are integers and a 3 px
// line centred on point p covers p-1..p+1, i.e. continuous p+0.5, so each half-pixel SVG
// coordinate maps to floor(s - 0.5). Circles keep their SVG geometry exactly.

static const lv_point_precise_t TIMER_HAND[]  = { {13, 15}, {13, 10} };
static const lv_point_precise_t TIMER_CROWN[] = { {10, 3},  {16, 3} };
static const lv_point_precise_t TIMER_STEM[]  = { {13, 3},  {13, 6} };
static const lv_point_precise_t CAL_RULE[]    = { {4, 12},  {23, 12} };
static const lv_point_precise_t CAL_POST_L[]  = { {9, 3},   {9, 8} };
static const lv_point_precise_t CAL_POST_R[]  = { {17, 3},  {17, 8} };
static const lv_point_precise_t SLIDE_TOP[]   = { {3, 8},   {23, 8} };
static const lv_point_precise_t SLIDE_BOT[]   = { {3, 18},  {23, 18} };

static void add_line(lv_obj_t *ic, const lv_point_precise_t *pts)
{
    lv_obj_t *l = lv_line_create(ic);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_size(l, ICON_BOX, ICON_BOX);          // a box the caps cannot be clipped by
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
}

// A bordered circle or rounded rect: no fill unless paint_icon fills it (a knob).
static void add_shape(lv_obj_t *ic, int x, int y, int w, int h, int radius, bool knob)
{
    lv_obj_t *o = lv_obj_create(ic);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 3, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    if (knob) lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static void add_ring(lv_obj_t *ic, int x, int y, int d)
{
    lv_obj_t *a = lv_arc_create(ic);
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, x, y);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_obj_set_style_arc_width(a, 3, LV_PART_MAIN);
}

// Recolour every stroke of an icon: fg for lines, rings and borders; fill for a knob's body
// (a knob is filled with its segment's own fill, so it hides the line behind it).
static void paint_icon(lv_obj_t *ic, lv_color_t fg, lv_color_t fill)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(ic); i++) {
        lv_obj_t *c = lv_obj_get_child(ic, (int32_t)i);
        if (lv_obj_check_type(c, &lv_line_class)) {
            lv_obj_set_style_line_color(c, fg, 0);
        } else if (lv_obj_check_type(c, &lv_arc_class)) {
            lv_obj_set_style_arc_color(c, fg, LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_color(c, fg, 0);
            if (lv_obj_get_style_bg_opa(c, LV_PART_MAIN) > LV_OPA_TRANSP) lv_obj_set_style_bg_color(c, fill, 0);
        }
    }
}

// Keyed by app id: the three glyphs the boards define. An unknown app gets an empty container
// (its segment still fills and the disc still names it).
//
// Icons are children of the root, created after the bands and the sweep arc, so the sweep
// passes over the bands and under every icon (the arriving arc slides under the destination
// icon). Consequence for #87: lifting TT_NO_LAYER_FX would fade a band without its icon.
static lv_obj_t *build_icon(lv_obj_t *root, const char *id, int seg)
{
    int x, y;
    tt_ring_seg_centre(seg, (TT_RING.r_in + TT_RING.r_out) / 2, &x, &y);   // r=149
    lv_obj_t *ic = lv_obj_create(root);
    lv_obj_remove_style_all(ic);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ic, ICON_BOX, ICON_BOX);
    lv_obj_set_pos(ic, x - ICON_BOX / 2, y - ICON_BOX / 2);
    if (!strcmp(id, "pomodist")) {              // stopwatch
        add_ring(ic, 3, 5, 21);                 // r 9 stroke: 21 px across, centre (13.5, 15.5)
        add_line(ic, TIMER_HAND);
        add_line(ic, TIMER_CROWN);
        add_line(ic, TIMER_STEM);
    } else if (!strcmp(id, "cal")) {            // calendar
        add_shape(ic, 3, 5, 22, 20, 4, false);  // 19x17 at (4.5, 6.5), rx 3, plus the 3 px stroke
        add_line(ic, CAL_RULE);
        add_line(ic, CAL_POST_L);
        add_line(ic, CAL_POST_R);
    } else if (!strcmp(id, "settings")) {       // two sliders
        add_line(ic, SLIDE_TOP);
        add_line(ic, SLIDE_BOT);
        add_shape(ic, 5, 4, 10, 10, LV_RADIUS_CIRCLE, true);    // r 3.5 at (10, 9)
        add_shape(ic, 13, 14, 10, 10, LV_RADIUS_CIRCLE, true);  // r 3.5 at (18, 19)
    } else {
        ESP_LOGW(TAG, "ring: no icon for %s", id);   // an app with no mapping draws a bare band; the log is how it shows up
    }
    return ic;
}

// ---- segments ---------------------------------------------------------------------------

static int norm360(int a) { return ((a % 360) + 360) % 360; }

// Colour one occupied segment: band fill, icon stroke, knob fill.
static void paint_col(int seg, lv_color_t fill, lv_color_t fg)
{
    lv_obj_set_style_arc_color(s_seg[seg].arc, fill, LV_PART_MAIN);
    if (s_seg[seg].ic) paint_icon(s_seg[seg].ic, fg, fill);
}

// D1: occupied = track + ink icon; selected = slate + paper icon. Empty slots are built once
// with no fill and never repainted.
static void paint(int seg, bool selected)
{
    if (!(s_mask & (1 << seg))) return;
    if (selected) paint_col(seg, TT_SLATE, TT_PAPER);
    else          paint_col(seg, TT_RING_TRACK, TT_INK);
}

static void build_seg(lv_obj_t *root, int seg)
{
    seg_t *s = &s_seg[seg];
    lv_obj_t *a = lv_arc_create(root);
    s->arc = a;
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(a, 2 * TT_RING.r_out, 2 * TT_RING.r_out);
    lv_obj_set_pos(a, TT_RING.cx - TT_RING.r_out, TT_RING.cy - TT_RING.r_out);
    // LVGL angles run clockwise from 3 o'clock; the ring's from 12. The drawn arc is 43
    // degrees. lv_arc angles are whole degrees here, so the extra degree of the odd 43 sits on
    // the trailing edge and the arc is 0.5 degree clockwise of centre (about 1.5 px at r=172).
    int c = seg * SEG_DEG - 90;
    lv_arc_set_bg_angles(a, (lv_value_precise_t)norm360(c - (SEG_DEG - GAP_DEG) / 2),
                            (lv_value_precise_t)norm360(c + (SEG_DEG - GAP_DEG + 1) / 2));
    lv_obj_set_style_arc_width(a, TT_RING.r_out - TT_RING.r_in, LV_PART_MAIN);
    s->ic = s->dot = NULL;
    if (s_mask & (1 << seg)) {
        paint(seg, seg == s_sel);        // the band now; the icon is built later (home_on_start) and painted then
    } else {
        // D1: an empty slot has no fill (arc_opa, never object opacity, D16) and one 4 px
        // marker at the band's mid radius.
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        int x, y;
        tt_ring_seg_centre(seg, (TT_RING.r_in + TT_RING.r_out) / 2, &x, &y);
        s->dot = lv_obj_create(a);
        lv_obj_remove_style_all(s->dot);
        lv_obj_remove_flag(s->dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s->dot, 4, 4);
        lv_obj_set_pos(s->dot, x - 2 - (TT_RING.cx - TT_RING.r_out), y - 2 - (TT_RING.cy - TT_RING.r_out));
        lv_obj_set_style_radius(s->dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s->dot, TT_TICK, 0);
        lv_obj_set_style_bg_opa(s->dot, LV_OPA_COVER, 0);
    }
}

// The travelling arc (5.12.3): built as a band is (same size, position and width) in slate,
// hidden at rest, created after the eight bands and before the icons.
static void build_sweep(lv_obj_t *root)
{
    lv_obj_t *a = lv_arc_create(root);
    s_sweep = a;
    s_sweeping = false;
    s_sw_cov = false;
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(a, 2 * TT_RING.r_out, 2 * TT_RING.r_out);
    lv_obj_set_pos(a, TT_RING.cx - TT_RING.r_out, TT_RING.cy - TT_RING.r_out);
    lv_obj_set_style_arc_width(a, TT_RING.r_out - TT_RING.r_in, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, TT_SLATE, LV_PART_MAIN);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
}

static void build_icons(lv_obj_t *root)
{
    for (int seg = 0; seg < TT_RING_SEGS; seg++) {
        if (!(s_mask & (1 << seg))) continue;
        s_seg[seg].ic = build_icon(root, trev_app_def(s_seg[seg].reg)->id, seg);
        paint(seg, seg == s_sel);
    }
}

// ---- disc -------------------------------------------------------------------------------
//
// One centred column, gap 8, from the mockup: name (box 32) / one-liner (box 20) / status
// (box 16) is 84 px tall, so top y=138 and the mocked centres are y=154, 188 and 214. Placed
// by centre, not by the CSS top edge, because the Plex bitmaps' line heights differ from the
// canvas's (placement convention). Offsets below are from the disc centre (180).
#define DISC_NAME_DY   (154 - 180)
#define DISC_LINE_DY   (188 - 180)
#define DISC_STATUS_DY (214 - 180)

// A single-line label truncated with dots at the column width. LONG_DOT does not truncate
// without an explicit height, so the height is the font's line.
static lv_obj_t *disc_label(lv_obj_t *disc, const lv_font_t *font, lv_color_t col, int dy)
{
    lv_obj_t *l = tt_label(disc, "", font, col, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_size(l, DISC_TEXT_W, lv_font_get_line_height(font));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    return l;
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

// The name is TT_F_HEAD 28, dropping to TT_F_TITLE 22 when it would not fit the column.
static void set_name(const char *name)
{
    lv_point_t sz;
    lv_text_get_size(&sz, name, TT_F_HEAD, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *f = sz.x > DISC_TEXT_W ? TT_F_TITLE : TT_F_HEAD;
    lv_obj_set_style_text_font(s_name, f, 0);
    lv_obj_set_height(s_name, lv_font_get_line_height(f));
    set_text(s_name, name);
}

static void build_disc(lv_obj_t *root)
{
    int d = 2 * TT_RING.r_disc;
    s_disc = lv_obj_create(root);
    lv_obj_remove_style_all(s_disc);
    lv_obj_remove_flag(s_disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_disc, d, d);
    lv_obj_set_pos(s_disc, TT_RING.cx - TT_RING.r_disc, TT_RING.cy - TT_RING.r_disc);
    lv_obj_set_style_radius(s_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_disc, TT_PAPER_ALT, 0);   // no rule ring: the fill alone reads as a button
    lv_obj_set_style_bg_opa(s_disc, LV_OPA_COVER, 0);
    s_name   = disc_label(s_disc, TT_F_HEAD, TT_INK, DISC_NAME_DY);
    s_line   = disc_label(s_disc, TT_F_BODY, TT_DESC, DISC_LINE_DY);

    // Status: HH:MM, or under the low-battery threshold three labels in a row, gap 8:
    // HH:MM and a middle dot in TT_DESC, then the percent and LOW in TT_INK (D9, ring-c).
    lv_obj_t *row = lv_obj_create(s_disc);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, TT_GAP, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, DISC_STATUS_DY);
    s_st[0] = tt_label(row, "", TT_F_STATUS, TT_DESC, 0);
    s_st[1] = tt_label(row, TT_GLYPH_MIDDOT, TT_F_STATUS, TT_DESC, 0);
    s_st[2] = tt_label(row, "", TT_F_STATUS, TT_INK, 0);
    lv_obj_add_flag(s_st[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_st[2], LV_OBJ_FLAG_HIDDEN);
}

static void show(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == on) {
        if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// One line per change of what the disc says (name or one-liner), so a shot can assert the
// copy without reading pixels. Not per refresh: set_state runs once a second.
static void log_disc(const char *name, const char *line)
{
    static char last[96];
    char cur[96];
    snprintf(cur, sizeof cur, "%s|%s", name, line);
    if (!strcmp(cur, last)) return;
    strcpy(last, cur);
    ESP_LOGI(TAG, "[ring] disc \"%s\" \"%s\"", name, line);
}

// The one-liner (per app) and the status line come from the board's hooks and change while the
// face is up (a sync ages, the minute turns), so both are re-read once a second.
static void set_state(void)
{
    char buf[48] = "";
    const trev_app_def_t *d = s_sel >= 0 ? trev_app_def(s_seg[s_sel].reg) : NULL;
    for (int i = 0; d && i < s_n_line_fn; i++)
        if (!strcmp(s_line_fn[i].id, d->id)) { s_line_fn[i].fn(buf, sizeof buf); break; }
    set_text(s_line, buf);
    log_disc(lv_label_get_text(s_name), buf);

    // "HH:MM" or "HH:MM · 12% LOW": everything after the first " · " is the alert, in ink.
    char line[48] = "";
    if (s_status_fn) s_status_fn(line, sizeof line);
    const char *sep = strstr(line, " " TT_GLYPH_MIDDOT " ");
    if (sep) {
        char head[24];
        size_t n = (size_t)(sep - line);
        if (n >= sizeof head) n = sizeof head - 1;
        memcpy(head, line, n);
        head[n] = '\0';
        set_text(s_st[0], head);
        set_text(s_st[2], sep + strlen(" " TT_GLYPH_MIDDOT " "));
    } else {
        set_text(s_st[0], line);
    }
    show(s_st[1], sep != NULL);
    show(s_st[2], sep != NULL);
}

static void set_disc(void)
{
    const trev_app_def_t *d = s_sel >= 0 ? trev_app_def(s_seg[s_sel].reg) : NULL;
    set_name(d && d->name ? d->name : "");
    set_state();
    s_state_ms = lv_tick_get();
}

// ---- input ------------------------------------------------------------------------------

static lv_timer_t *s_open_t;             // the pending open, at most one

// The open runs on the next LVGL tick, never inside the input callback: trev_open deletes the
// screen the callback is still standing on (ring root, arcs, this very handler's widgets).
static void open_cb(lv_timer_t *t)
{
    int reg = (int)(intptr_t)lv_timer_get_user_data(t);
    s_open_t = NULL;                     // LVGL frees the one-shot after we return; on_stop must not
    trev_open(reg);
}

static void open_seg(int seg)
{
    if (s_open_t) return;                // a second tap before the tick is the same tap
    s_open_t = lv_timer_create(open_cb, 1, (void *)(intptr_t)s_seg[seg].reg);
    lv_timer_set_repeat_count(s_open_t, 1);
}

// D16: a new detent cancels whatever the entrance is still doing to the two arcs it touches
// and puts them at rest, so no half-faded or half-scaled segment survives a selection move.
// Under TT_NO_LAYER_FX the entrance effects are no-ops, so there is nothing to cancel and
// selection is the instant colour swap in paint().
static void settle(int seg)
{
#if !TT_NO_LAYER_FX
    if (seg < 0 || !s_seg[seg].arc) return;
    lv_anim_delete(s_seg[seg].arc, NULL);
    lv_obj_set_style_opa(s_seg[seg].arc, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(s_seg[seg].arc, LV_SCALE_NONE, 0);
#else
    (void)seg;
#endif
}

// ---- sweep (Task 5.12.3) ----------------------------------------------------------------
//
// On a felt detent the slate does not jump: a third arc travels from the old slot to the new at
// 45 degrees per slot in 150 ms whatever the span (empties included), and the segment is painted
// slate when it lands. The exec takes the arc's centre in unwrapped ring degrees (0 at 12
// o'clock, clockwise); tt_step retargets from the live centre. s_sel is the destination
// throughout, painted track and ink until the arc covers it.
#define SWEEP_HALF ((SEG_DEG - GAP_DEG) / 2)       // 21: the arc covers a segment within this of its centre

static void sweep_exec(void *var, int32_t v)
{
    int c = v - 90;                                // the ring's degrees to LVGL's, as build_seg
    lv_arc_set_bg_angles((lv_obj_t *)var, (lv_value_precise_t)norm360(c - SWEEP_HALF),
                                          (lv_value_precise_t)norm360(c + (SEG_DEG - GAP_DEG + 1) / 2));
    lv_obj_remove_flag((lv_obj_t *)var, LV_OBJ_FLAG_HIDDEN);
    if (s_sel < 0 || !s_seg[s_sel].ic) return;
    int d = norm360(v - s_sel * SEG_DEG + 180) - 180;
    bool cov = d >= -SWEEP_HALF && d <= SWEEP_HALF;
    if (cov == s_sw_cov) return;                   // repaint only when the icon's side changes
    s_sw_cov = cov;
    if (cov) paint_icon(s_seg[s_sel].ic, TT_PAPER, TT_SLATE);      // knob fill = the arc under it
    else     paint_icon(s_seg[s_sel].ic, TT_INK, TT_RING_TRACK);
}

static void sweep_settle(void *var)
{
    lv_obj_add_flag((lv_obj_t *)var, LV_OBJ_FLAG_HIDDEN);
    s_sweeping = false;
    s_sw_cov = false;
    if (s_sel >= 0) paint(s_sel, true);
}

// A tap lands the sweep at once.
static void land_sweep(void)
{
    if (s_sweeping) tt_step(s_sweep, sweep_exec, 0, s_sw_target, 0, sweep_settle);
}

// The selected segment as it should look right now: slate at rest, the track band (the arc
// is what is slate) with the icon on its current side while a sweep runs.
static void repaint_sel(void)
{
    if (s_sel < 0) return;
    if (!s_sweeping) { paint(s_sel, true); return; }
    lv_obj_set_style_arc_color(s_seg[s_sel].arc, TT_RING_TRACK, LV_PART_MAIN);
    if (s_seg[s_sel].ic) {
        if (s_sw_cov) paint_icon(s_seg[s_sel].ic, TT_PAPER, TT_SLATE);
        else          paint_icon(s_seg[s_sel].ic, TT_INK, TT_RING_TRACK);
    }
}

// Move the selection, restyling only the two arcs that change. paint_dest false leaves the
// destination to the sweep.
static void select_seg(int seg, bool paint_dest)
{
    int old = s_sel;
    if (seg == old) return;
    settle(old);
    settle(seg);
    s_sel = seg;
    if (old >= 0) paint(old, false);
    if (paint_dest) paint(seg, true);
    set_disc();
}

// D18: with haptics off a detent has no feel, so the newly selected segment flashes to ink for
// 80 ms and then restores its slate. A new detent cancels a flash still running.
static void flash_end(lv_timer_t *t)
{
    (void)t;
    s_flash_t = NULL;                    // LVGL frees the one-shot after this returns
    if (!s_seg[s_flash_seg].arc) return;
    if (s_flash_seg == s_sel) repaint_sel();   // what the sweep shows there, or slate at rest
    else                      paint(s_flash_seg, false);
}

static void cancel_flash(void)
{
    if (!s_flash_t) return;
    lv_timer_delete(s_flash_t);
    s_flash_t = NULL;
}

static void flash(int seg)
{
    paint_col(seg, TT_INK, TT_PAPER);    // the icon stays paper; only the fill goes to ink
    s_flash_seg = seg;
    s_flash_t = lv_timer_create(flash_end, 80, NULL);
    lv_timer_set_repeat_count(s_flash_t, 1);
}

// D6: the wheel skips empty slots and wraps; nothing happens when the selection cannot move.
// Returns whether the selection moved: the wheel path ticks only on true (D6).
static bool home_turn(trev_turn_t dir)
{
    bool flashing = s_flash_t != NULL;
    cancel_flash();
    int old = s_sel;
    int next = tt_ring_next(old, (int)dir, (uint8_t)s_mask);
    if (next < 0 || next == old) {
        if (flashing) repaint_sel();     // the cancelled flash left it ink
        return false;
    }
    if (old < 0 || !s_sweep) {           // nothing to sweep from
        select_seg(next, true);
    } else {
        // The sweep's target: where the running one is going (or the origin's centre), plus the
        // slots stepped in the detent's direction, empties included. The live centre is the
        // start, so a detent mid-sweep never jumps.
        int steps = (((next - old) * (int)dir) % TT_RING_SEGS + TT_RING_SEGS) % TT_RING_SEGS;
        int32_t from = tt_step_live(s_sweep, sweep_exec, old * SEG_DEG);
        s_sw_target = (s_sweeping ? s_sw_target : old * SEG_DEG) + (int)dir * steps * SEG_DEG;
        select_seg(next, false);         // the origin back to track and ink, the disc text swapped, at the tick
        s_sw_cov = false;                // the destination is track and ink until the arc covers it
        s_sweeping = true;
        tt_step(s_sweep, sweep_exec, from, s_sw_target, TT_STEP_MS, sweep_settle);
    }
    if (s_haptics_on && !s_haptics_on()) flash(next);
    return true;
}
static void home_on_turn(trev_app_t *a, trev_turn_t dir) { (void)a; home_turn(dir); }
static bool home_on_wheel(trev_app_t *a, trev_turn_t dir) { (void)a; return home_turn(dir); }

// A tap on the disc opens the selection; on a segment it selects that segment and opens it
// (so a tap on the selected one just opens); on an empty slot or outside the band it does
// nothing (D6, D13). trev_last_tap holds the press position (display pixels).
static void home_on_gesture(trev_app_t *a, trev_gesture_t g)
{
    (void)a;
    if (g != TREV_GESTURE_TAP_CONTENT) return;
    int x, y;
    trev_last_tap(&x, &y);
    int hit = tt_ring_hit(x, y);
    ESP_LOGI(TAG, "[ring] hit %d", hit);   // 0..7, TT_RING_DISC or -1, before the disc maps to the selection
    if (hit == TT_RING_DISC) hit = s_sel;
    if (hit < 0 || !(s_mask & (1 << hit))) return;
    land_sweep();                        // a tap lands the sweep at once (5.12.3)
    select_seg(hit, true);
    open_seg(hit);
}

// exists so trev_has_direct_touch() is true and every release is a content tap; split tap ownership from drag when a second tap-only face appears
static void home_on_drag(trev_app_t *a, int dx, int dy) { (void)a; (void)dx; (void)dy; }

// ---- lifecycle --------------------------------------------------------------------------

static void home_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    tt_face_ground(root, tt_skin_paper());
    build_slots();
    if (s_sel < 0 || !(s_mask & (1 << s_sel))) s_sel = tt_ring_next(-1, 1, (uint8_t)s_mask);
    for (int seg = 0; seg < TT_RING_SEGS; seg++) build_seg(root, seg);
    build_sweep(root);                   // after the bands, before the icons
    build_icons(root);
    // D13: publish where each segment's tap zone is, from the table the hit test reads, so
    // sim/zonecheck.sh taps these points instead of carrying its own copy of the geometry.
    for (int seg = 0; seg < TT_RING_SEGS; seg++) {
        int x, y;
        tt_ring_seg_centre(seg, (TT_RING.r_in + TT_RING.r_out) / 2, &x, &y);
        ESP_LOGI(TAG, "[ring] seg %d x=%d y=%d", seg, x, y);
    }
    build_disc(root);
    set_disc();

    // Entrance: the slate segment is the hero (by area, D2), the disc and the other occupied
    // segments fade in behind it 40 ms apart. Empties do not fade: they have no fill.
    lv_obj_t *rows[TT_RING_SEGS + 1];
    int nr = 0;
    rows[nr++] = s_disc;
    for (int seg = 0; seg < TT_RING_SEGS; seg++)
        if ((s_mask & (1 << seg)) && seg != s_sel) rows[nr++] = s_seg[seg].arc;
    tt_face_enter(root, s_sel >= 0 ? s_seg[s_sel].arc : s_disc, rows, nr);
}

static void home_on_tick(trev_app_t *a, uint32_t now_ms)
{
    (void)a; (void)now_ms;
    if (s_disc && lv_tick_elaps(s_state_ms) >= 1000) {
        set_state();
        s_state_ms = lv_tick_get();
    }
}

static void home_on_stop(trev_app_t *a)
{
    (void)a;
    if (s_open_t) { lv_timer_delete(s_open_t); s_open_t = NULL; }
    cancel_flash();
    memset(s_seg, 0, sizeof s_seg);
    s_sweep = NULL;                      // LVGL deletes its anim with the screen
    s_sweeping = s_sw_cov = false;
    s_disc = s_name = s_line = NULL;
    memset(s_st, 0, sizeof s_st);
}

const trev_app_def_t TREV_HOME_ROUND = {
    .api_version = TREV_APP_API_VERSION,
    .id = "home", .name = "Home",
    .on_start = home_on_start, .on_stop = home_on_stop, .on_tick = home_on_tick,
    .on_turn = home_on_turn, .on_wheel = home_on_wheel, .on_gesture = home_on_gesture, .on_drag = home_on_drag,
};

#endif // TT_ROUND_DISPLAY && TT_HOME_RING
