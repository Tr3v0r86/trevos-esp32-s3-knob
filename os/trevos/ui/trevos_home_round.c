// trevos_home_round.c — the round home face. A wall clock in the centre and one drawn
// pill per app, placed ON the tap zone that opens it: left zone = registry app 1, right zone
// = app 2, each named from the registry. Long-press-anywhere (the board's HOME gesture)
// returns here from any app.
//
// v2, 2026-08-29, from on-glass feedback: v1 used select-then-commit (TURN moves a highlight,
// COMMIT opens), which is wheel thinking on a wheelless disk. Nothing on the glass said where
// to tap, so tapping felt broken. Now the tap targets are visible buttons placed with
// tt_actionbar_geom(), the same numbers tt_zone_at() hit-tests with, so the button IS the
// zone and the two cannot drift. Centre zone deliberately does nothing: it is the clock.
//
// v3, 2026-08-29 (design task 4, blessed mockup): the bordered rectangles become drawn
// tt_pills in the shared kit's slate identity, and the date joins the clock as the centre
// group instead of a lone "TREVOS" sub-label. TREVOS itself moves to the top status rail,
// the same place every other round face names itself.
//
// Registration convention (2026-08-29 plan, ADR-0015): home 0, Pomodoist 1, app 2.
//
// E12 (ADR-0020): this is the disk's home. Under TT_HOME_RING=1 the puck's ring home
// (trevos_home_ring.c) exports TREV_HOME_ROUND instead and this body compiles out, so a
// board flips between the two with one define and both files stay compiled everywhere.
#include "trevos_theme.h"
#include "trevos_home.h"
#if TT_ROUND_DISPLAY && !TT_HOME_RING

#include "trevos.h"
#include "trevos_ui.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static lv_obj_t *s_clock, *s_date;

// The two pills name whatever apps sit at registry index 1 and 2 (POMODOIST + CAL on the
// TT_CAL set), uppercased when the face is built. Home
// does not know or care which app-core is which; ADR-0015 changed the set without touching it.
static char s_name[2][16];

static const char *slot_name(int slot)
{
    const trev_app_def_t *d = trev_app_def(slot + 1);
    const char *n = d && d->name ? d->name : "?";
    size_t i = 0;
    for (; n[i] && i < sizeof s_name[slot] - 1; i++) s_name[slot][i] = (char)toupper((unsigned char)n[i]);
    s_name[slot][i] = '\0';
    return s_name[slot];
}

static void set_clock(void)
{
    if (!s_clock) return;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    // 24, not 8: %d args widen to plain int at the snprintf boundary, so -Wformat-truncation
    // sizes against int's full range instead of the real 0-59 bound (same false positive
    // main.c's set_clock already works around).
    char buf[24];
    // Show --:-- before any clock source has landed (ADR-0008 forbids a wrong clock more
    // than a missing one; the RTC restore makes this near-instant on this board).
    bool clock_ok = tm.tm_year + 1900 >= 2020;
    if (!clock_ok) snprintf(buf, sizeof(buf), "--:--");
    else snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    // home_on_tick calls set_clock() every 100ms - only touch the label when its string
    // actually changed, same guard as main.c's set_clock (glass fixes round 2, Fix 1).
    if (strcmp(lv_label_get_text(s_clock), buf) != 0) lv_label_set_text(s_clock, buf);

    if (s_date) {
        char dbuf[16];
        if (!clock_ok) {
            snprintf(dbuf, sizeof(dbuf), "NO TIME");
        } else {
            strftime(dbuf, sizeof(dbuf), "%a %d %b", &tm);   // "Sat 29 Aug"
            for (char *p = dbuf; *p; p++) *p = (char)toupper((unsigned char)*p);
        }
        if (strcmp(lv_label_get_text(s_date), dbuf) != 0) lv_label_set_text(s_date, dbuf);
    }
}

// One drawn pill, centred on its tap zone's cell centre, sitting in the same bottom band
// every actionbar-based face uses. tt_actionbar() itself always draws small/secondary
// flank pills (trevos_ui.c), which is wrong here - the mockup's two home buttons are both
// full-size, filled slate - so this calls tt_pill() directly rather than routing through
// tt_actionbar(), but borrows its exact geometry so the visible pill stays on the zone
// tt_zone_at() fires (x0/w from tt_actionbar_geom(), the one shared source of truth).
static lv_obj_t *app_button(lv_obj_t *root, const char *name, int x0, int w, int slot)
{
    int third = w / 3;
    int dx = -w / 2 + slot * third + third / 2;   // slot 0 = left zone, slot 2 = right zone
    // Vertical band: matches tt_actionbar's own bar (LV_ALIGN_BOTTOM_MID, -TT_ROUND_INSET,
    // height TT_BAR_HINT) so home's pills sit in the same row every other round face's
    // action pills do. TT_ROUND_INSET is trevos_ui.c-private (not exposed via trevos_ui.h),
    // so the 40 is the same literal named in this task's brief ("actionbar top edge is at
    // y=290 = 360 - 40 - 30"); band centre = 360 - 40 - TT_BAR_HINT/2.
    int band_dy = -(40 + TT_BAR_HINT / 2);
    lv_obj_t *p = tt_pill(root, name, TT_SLATE, TT_PAPER, TT_SLATE, false, 0);
    lv_obj_align(p, LV_ALIGN_BOTTOM_MID, dx, band_dy);
    return p;
}

static void home_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(root, sk);

    // Thin rim track, the family resemblance to every other round face. No indicator: home
    // has no "how far" to answer, and v1's selection segment answered a question the face
    // no longer asks.
    lv_obj_t *track = lv_arc_create(root);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, lv_pct(96), lv_pct(96));
    lv_obj_center(track);
    lv_arc_set_bg_angles(track, 0, 360);
    lv_obj_set_style_arc_color(track, TT_RING_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(track, 4, LV_PART_MAIN);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);

    // Wordmark moves to the top status rail, where every other round face already names
    // itself. Home has no use for the rail's live clock slot - the centre group below IS
    // the wall clock, and a second one would be exactly the "two clocks is noise" case
    // deskbuddy_dash.c already flags - so the slot is hidden rather than left ticking.
    lv_obj_t *rail_clock = NULL;
    tt_statusbar(root, "TREVOS", sk, &rail_clock);
    if (rail_clock) lv_obj_add_flag(rail_clock, LV_OBJ_FLAG_HIDDEN);

    // Centre group: clock + 8px + date, stacked as one flex column so the gap is a single
    // token (TT_GAP) instead of two hand-tuned offsets. The band this sits in (status rail
    // bottom to button band top) is symmetric around the glass centre, so no offset needed.
    lv_obj_t *group = lv_obj_create(root);
    lv_obj_remove_style_all(group);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, TT_GAP, 0);
    lv_obj_center(group);

    s_clock = tt_label(group, "--:--", TT_F_NUM, TT_INK, 0);
    s_date  = tt_label(group, "--- -- ---", TT_F_LABEL_SM, TT_MUTED, TT_TRACK_WIDE);

    // Buttons on the zones that fire them: cell centres from the same geometry the input
    // glue hit-tests with.
    int x0, w;
    tt_actionbar_geom(&x0, &w);
    lv_obj_t *b0 = app_button(root, slot_name(0), x0, w, 0);
    lv_obj_t *b1 = app_button(root, slot_name(1), x0, w, 2);

    set_clock();

    lv_obj_t *rows[] = { s_date, b0, b1 };
    tt_face_enter(root, s_clock, rows, 3);
}

static void home_on_stop(trev_app_t *a)
{
    (void)a;
    s_clock = s_date = NULL;
}

static void home_on_tick(trev_app_t *a, uint32_t now_ms)
{
    (void)a; (void)now_ms;
    set_clock();
}

// Direct-open: the left zone's verb is PREV, the right zone's NEXT; each opens its button's
// app. No selection state to get lost in.
static void home_on_turn(trev_app_t *a, trev_turn_t dir)
{
    (void)a;
    trev_open(dir == TREV_TURN_PREV ? 1 : 2);
}

static void home_on_commit(trev_app_t *a)
{
    (void)a;                 // centre is the clock; deliberately not a control
}

const trev_app_def_t TREV_HOME_ROUND = {
    .api_version = TREV_APP_API_VERSION,
    .id = "home", .name = "Home",
    .on_start = home_on_start, .on_stop = home_on_stop, .on_tick = home_on_tick,
    .on_turn = home_on_turn, .on_commit = home_on_commit,
};

#endif // TT_ROUND_DISPLAY && !TT_HOME_RING
