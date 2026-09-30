// cal_face_rect.c - PLACEHOLDER glance face for a rectangular panel (the CYD, 240x320).
//
// ADR-0017 point 6: Claude Code does not design faces. This file exists to prove real data on
// glass: it renders cal_glance_t in plain type, top to bottom, and nothing else. The designed
// face replaces the layout here from the approved design; the rules stay in
// cal_glance.c. Compiled only with CAL_FACE_RECT=1; the round face (cal_faces.c) is the rest.
#if CAL_FACE_RECT
#include "cal_ui.h"
#include "cal_sync.h"
#include "cal_glance.h"
#include "trevos_theme.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef TT_DEV_CAL_NOW
#define TT_DEV_CAL_NOW -1      // sim only: pin "now" in minutes since midnight so shots are stable
#endif

#define PAD 12

static cal_day_t s_today, s_tomorrow;   // copies; the face never points into cal_sync
static bool      s_have_today, s_have_tomorrow;
static uint32_t  s_gen = UINT32_MAX;
static int       s_min = -1;
static int       s_w;                    // text width: panel width less padding
static lv_obj_t *l_clock, *l_sync, *l_hero, *l_sub, *l_chip, *l_list;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    if (w > 0) { lv_obj_set_width(l, w); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); }
    lv_label_set_text(l, "");
    return l;
}

// Wrap up to max_lines, then end in an ellipsis rather than push everything below off the glass.
static void fit(lv_obj_t *l, const char *text, int max_lines)
{
    int cap = max_lines * lv_font_get_line_height(lv_obj_get_style_text_font(l, 0));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    lv_label_set_text(l, text);
    lv_obj_update_layout(l);
    if (lv_obj_get_height(l) > cap) { lv_obj_set_height(l, cap); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); }
}

static void date_str(time_t t, char out[11])
{
    struct tm tm; localtime_r(&t, &tm);
    strftime(out, 11, "%Y-%m-%d", &tm);
}

static void load_days(time_t now)
{
    char d[11];
    date_str(now, d);            s_have_today    = cal_sync_get_day(d, &s_today);
    date_str(now + 86400, d);    s_have_tomorrow = cal_sync_get_day(d, &s_tomorrow);
    if (s_have_today) cal_day_sort(&s_today);
    if (s_have_tomorrow) cal_day_sort(&s_tomorrow);
}

static void render(time_t now, int now_min, bool clock_ok)
{
    time_t last = 0;
    bool synced = cal_sync_last_sync(&last);
    cal_glance_t g;
    cal_glance(s_have_today ? &s_today : NULL, s_have_tomorrow ? &s_tomorrow : NULL, now_min,
               clock_ok, cal_sync_generation() > 0, synced ? (long)(now - last) : -1, &g);

    char buf[160], dur[16];
    if (clock_ok) snprintf(buf, sizeof buf, "%02d:%02d", now_min / 60, now_min % 60);
    else          snprintf(buf, sizeof buf, "--:--");
    lv_label_set_text(l_clock, buf);

    if (synced) {
        struct tm tm; localtime_r(&last, &tm);
        snprintf(buf, sizeof buf, "updated %02d:%02d", tm.tm_hour, tm.tm_min);
    } else snprintf(buf, sizeof buf, "not synced");
    lv_label_set_text(l_sync, buf);
    lv_obj_set_style_text_color(l_sync, g.stale ? TT_CORAL : TT_MUTED, 0);

    const cal_day_t *nd = g.next_tomorrow ? &s_tomorrow : &s_today;
    const cal_event_t *next = g.next_ev >= 0 ? &nd->ev[g.next_ev] : NULL;
    char nxt[96] = "";
    if (next) snprintf(nxt, sizeof nxt, "%s %02d:%02d  %s", g.next_tomorrow ? "Tomorrow" : "Next",
                       next->sh, next->sm, next->title);

    const char *hero = "", *sub = "";
    char hb[96], sb[128];
    switch (g.state) {
    case CAL_G_CLOCK_UNSET: hero = "Waiting for laptop"; sub = "Plug in USB and run push-cal"; break;
    case CAL_G_WAITING:     hero = "No calendar yet";    sub = "Plug in USB and run push-cal"; break;
    case CAL_G_NO_TODAY:    hero = "Couldn't load today"; sub = "Showing nothing rather than a wrong day"; break;
    case CAL_G_UNSURE:
        hero = "Calendar incomplete";
        snprintf(sb, sizeof sb, "%d events not shown", g.more); sub = sb; break;
    case CAL_G_BUSY:
        if (g.now_extra) snprintf(hb, sizeof hb, "%s (+%d)", s_today.ev[g.now_ev].title, g.now_extra);
        else             snprintf(hb, sizeof hb, "%s", s_today.ev[g.now_ev].title);
        hero = hb;
        cal_glance_dur(g.mins, dur);
        snprintf(sb, sizeof sb, "ends in %s%s%s", dur, next ? "\n" : "", nxt); sub = sb; break;
    case CAL_G_FREE:
        hero = "Free";
        cal_glance_dur(g.mins, dur);
        snprintf(sb, sizeof sb, "for %s\n%s", dur, nxt); sub = sb; break;
    case CAL_G_DONE:  hero = "Free for today"; sub = nxt; break;
    case CAL_G_EMPTY: hero = "No meetings";    sub = nxt; break;
    }
    fit(l_hero, hero, 2);
    fit(l_sub, sub, 3);

    if (g.allday && s_have_today) {
        for (int i = 0; i < s_today.n; i++) if (s_today.ev[i].allday) {
            if (g.allday > 1) snprintf(buf, sizeof buf, "All day: %s (+%d)", s_today.ev[i].title, g.allday - 1);
            else              snprintf(buf, sizeof buf, "All day: %s", s_today.ev[i].title);
            break;
        }
        lv_label_set_text(l_chip, buf);
    } else lv_label_set_text(l_chip, "");

    // Later today, after whatever the hero and sub lines already name. Lean-in detail only.
    char list[320] = ""; int rows = 0, left = 0, n = 0;
    if (s_have_today && g.state != CAL_G_CLOCK_UNSET)
        for (int i = 0; i < s_today.n; i++) {
            const cal_event_t *e = &s_today.ev[i];
            if (e->allday || e->sh * 60 + e->sm <= now_min || (!g.next_tomorrow && i == g.next_ev)) continue;
            if (rows == 4) { left++; continue; }
            n += snprintf(list + n, sizeof list - n, "%s%02d:%02d  %.22s", rows ? "\n" : "", e->sh, e->sm, e->title);
            rows++;
        }
    if (left || (g.more && g.state != CAL_G_UNSURE))
        snprintf(list + n, sizeof list - n, "%s+%d more", rows ? "\n" : "", left + g.more);
    lv_label_set_text(l_list, list);   // flex-grow + dot: whatever does not fit ends in "..." 
}

static void refresh(bool force)
{
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    bool clock_ok = tm.tm_year + 1900 >= 2020;
    int now_min = TT_DEV_CAL_NOW >= 0 ? TT_DEV_CAL_NOW : tm.tm_hour * 60 + tm.tm_min;
    uint32_t gen = cal_sync_generation();
    if (!force && gen == s_gen && now_min == s_min) return;
    if (force || gen != s_gen || now_min == 0) load_days(now);   // midnight: today moves
    s_gen = gen; s_min = now_min;
    render(now, now_min, clock_ok);
}

static void cal_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    s_w = lv_display_get_horizontal_resolution(NULL) - 2 * PAD;
    lv_obj_set_style_bg_color(root, TT_PAPER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, PAD, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, s_w, LV_SIZE_CONTENT);
    l_clock = label(bar, &plex_sans_b_16, TT_INK, 0);
    l_sync  = label(bar, TT_F_STATUS, TT_MUTED, 0);
    lv_obj_align(l_clock, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(l_sync, LV_ALIGN_RIGHT_MID, 0, 0);

    l_hero = label(root, &plex_sans_b_28, TT_INK, s_w);
    l_sub  = label(root, &plex_sans_b_22, TT_SLATE, s_w);
    l_chip = label(root, &plex_sans_r_14, TT_DESC, s_w);
    l_list = label(root, &plex_sans_r_14, TT_INK, s_w);
    lv_obj_set_flex_grow(l_list, 1);
    lv_label_set_long_mode(l_list, LV_LABEL_LONG_DOT);
    refresh(true);
}

static void cal_on_stop(trev_app_t *app) { (void)app; l_clock = l_sync = l_hero = l_sub = l_chip = l_list = NULL; }
static void cal_on_tick(trev_app_t *app, uint32_t now_ms) { (void)app; (void)now_ms; if (l_hero) refresh(false); }
static void cal_on_turn(trev_app_t *app, trev_turn_t dir) { (void)app; (void)dir; }   // nothing to navigate yet
static void cal_on_commit(trev_app_t *app) { (void)app; }

const trev_app_def_t CAL_APP = {
    .api_version = TREV_APP_API_VERSION,
    .id = "cal", .name = "Cal",
    .on_start = cal_on_start, .on_stop = cal_on_stop, .on_tick = cal_on_tick,
    .on_turn = cal_on_turn, .on_commit = cal_on_commit,
};

bool cal_chime_due(void) { return false; }   // no speaker wired on the rect board yet
#endif
