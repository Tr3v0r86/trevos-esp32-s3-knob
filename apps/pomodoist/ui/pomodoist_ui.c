#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "pomodoist_ui.h"
#include "pomodoist_core.h"
#include "pomodoist_sync.h"
#include "pomodoist_outbox.h"
#include "trevos.h"
#include "trevos_theme.h"
#include "trevos_ui.h"
#include "trev_bar2.h"
#include "trev_net.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "nvs.h"
#ifndef TT_DEV_DEMO
#define TT_DEV_DEMO 0
#endif
#ifndef TT_DEV_VIEW
#define TT_DEV_VIEW 0
#endif
#ifndef TT_TODOIST_WRITE
#define TT_TODOIST_WRITE 1
#endif
static const char *TAG = "pomodoist";
#define POMO_BREAK_SECS 300

// D1: runtime-tunable settings (editable on-device in the Settings app, persisted to NVS
// "cfg"). The break length is now a runtime var (seeded from POMO_BREAK_SECS / the setting)
// so Settings can change it; the Todoist write kill-switch lives in the sync module
// (pomodoist_sync_set_write) because it gates the POST, never the outbox record.
static uint32_t s_break_secs = POMO_BREAK_SECS;

EXT_RAM_BSS_ATTR static pomo_core_t s_pc;
// PV_LEDGER (C11) is round-only in practice: nothing reaches it without the swipe-down
// gesture, which only the round build routes. The enum member is unconditional so the
// rectangular boards compile the same switch.
typedef enum { PV_FOCUS, PV_PICKER, PV_BREAK, PV_LEDGER } pomo_view_t;
static pomo_view_t s_view;
static bool s_face_break;
static bool s_setmode;   // focus set-mode (A3): editing the block length while idle
static lv_obj_t *s_pomo_root;
static uint32_t s_break_left = POMO_BREAK_SECS;   // seeded; reset to s_break_secs on each break entry
static uint32_t s_break_prev, s_break_acc;   // break countdown ms bookkeeping (tick is 100ms)
static lv_obj_t *p_clock, *p_arc, *p_time, *p_rundot, *p_eyebrow, *p_title, *p_tagrow, *p_descbox, *p_desc, *p_break_clock;
static lv_obj_t *p_phase, *p_hint;   // FOCUS/SET phase label + hint bar (swapped in set-mode, A3)
static lv_obj_t *p_len;              // the puck focus face's wheel readout label ("25 MIN"); NULL on every other face
// The puck's rim (pomo-focus.dc.html: r 168..176). Every other board keeps the 336 / 12 it has.
#define POMO_RIM_D 352
#define POMO_RIM_W 8
// D18: the detail card, round only. NOT a pomo_view_t: it is a child laid over whatever face
// is mounted, so the face underneath survives untouched and closing it is a delete, not a
// rebuild. Non-NULL means it is open, and that is the whole state.
static lv_obj_t *p_detail;
static lv_obj_t *p_break_arc;        // round only: the rim as the break countdown. NULL elsewhere.
static void focus_hint_refresh(void);   // fwd: render() swaps the hint bar with the phase

// Bottom bar, per input model. A touch board's three tap zones ARE its buttons, so label
// them where they are (D1); a two-button board names KEY and BOOT. Same three verbs either
// way: `l` = left zone / KEY-long, `m` = centre / BOOT, `r` = right zone / KEY.
static lv_obj_t *pomo_bar(lv_obj_t *root, const char *l, const char *m, const char *r,
                          const tt_skin_t *sk)
{
    return tt_actionbar(root, l, m, r, sk);
}
static void pomo_flash_complete(void);   // fwd (D3): end-of-pomo visual cue on the focus face

// ---- timer persistence (A2): survive reboot via NVS "pomo" namespace ----
// We persist only the timer state (length / remaining / running / active task index),
// not the task list — the list is owned by todoist-sync's own cache. On restore we
// overlay these onto the freshly seeded/synced core so a sync is never clobbered.
#define POMO_NVS_NS "pomo"

static void pomo_persist_save(void)
{
    nvs_handle_t h;
    if (nvs_open(POMO_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "total_s", s_pc.total_s);
    nvs_set_u32(h, "left_s",  s_pc.left_s);
    nvs_set_u8 (h, "running", s_pc.running ? 1 : 0);
    nvs_set_i32(h, "active",  s_pc.active);
    esp_err_t err = nvs_commit(h);   // surface a persistent flash failure; save is best-effort otherwise
    if (err != ESP_OK) ESP_LOGW(TAG, "pomo persist commit failed: %s", esp_err_to_name(err));
    nvs_close(h);
}

// Restore timer fields over an already-init/synced core. Missing keys = first boot (no-op).
static void pomo_persist_restore(void)
{
    nvs_handle_t h;
    if (nvs_open(POMO_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint32_t total = 0, left = 0; uint8_t run = 0; int32_t active = 0;
    bool have_total = nvs_get_u32(h, "total_s", &total) == ESP_OK;
    bool have_left  = nvs_get_u32(h, "left_s",  &left)  == ESP_OK;
    bool have_run   = nvs_get_u8 (h, "running", &run)   == ESP_OK;
    bool have_act   = nvs_get_i32(h, "active",  &active) == ESP_OK;
    nvs_close(h);
    if (have_total && total >= POMO_MIN_MINUTES * 60 && total <= POMO_MAX_MINUTES * 60) {
        s_pc.total_s = total;
        s_pc.left_s  = (have_left && left <= total) ? left : total;
        s_pc.running = have_run && run;

        // A stored left_s of 0 means the block FINISHED before this boot. Restoring that
        // verbatim is faithful and wrong twice over (issue #39):
        //
        //   - the face rests at 00:00 against a fully elapsed ring, which is correct and
        //     indistinguishable from broken. ADR-0008 forbids a permanent "--" for exactly this
        //     reason, and a dead-zero timer is the same failure wearing a different costume.
        //   - a finished block still flagged running re-fires its completion on the next boot,
        //     dropping the device straight into a break that should have followed the block in
        //     real time, possibly hours earlier. That is how the 1.85B booted into the break
        //     face on 2026-08-29.
        //
        // A fresh boot presents an armed timer instead. Nothing is lost: the completion already
        // drove the break face and the Todoist write-back in the session that earned it, and
        // this only governs what a REBOOT resumes into.
        if (s_pc.left_s == 0) { s_pc.left_s = s_pc.total_s; s_pc.running = false; }

        if (s_pc.running) { s_pc._prev_ms = 0; s_pc._acc_ms = 0; }   // re-anchor; no time elapses while powered off
    }
    if (have_act && s_pc.tasks.count > 0) {   // clamp to the synced list; never index past it
        s_pc.active = (active >= 0 && active < s_pc.tasks.count) ? active : 0;
        s_pc.cursor = s_pc.active;
    }
    s_pc.dirty = true;
}

// ---- device settings persistence (D1): NVS "cfg" namespace ----
// Two user-tunable settings: break minutes, todoist-write on/off. Loaded
// + applied at boot (app_main) BEFORE trev_open; nothing writes them since the Settings app
// was deleted (T13), so they hold whatever an earlier build saved. The focus length is not
// here: the persisted "pomo" total_s is its one home on every board (#93). Kept in a
// separate namespace from the timer state ("pomo") so config and live-timer never collide.
#define CFG_NVS_NS "cfg"

// In-memory config mirror. Seeded from the compile defaults,
// overwritten by cfg_load() at boot. Minutes are validated against POMO_MIN/MAX before use.
static uint32_t s_cfg_break_min = POMO_BREAK_SECS / 60;   // 1..20
static bool     s_cfg_tdwrite   = (TT_TODOIST_WRITE != 0);

// Load config into the in-memory mirror (missing keys keep the compile defaults). Clamps to
// valid ranges so a corrupt/garbage NVS value can never poison the timer.
static void cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint32_t bm = 0; uint8_t tw = 0;
    if (nvs_get_u32(h, "break_min", &bm) == ESP_OK &&
        bm >= 1 && bm <= 20) s_cfg_break_min = bm;
    if (nvs_get_u8 (h, "tdwrite",  &tw) == ESP_OK) s_cfg_tdwrite = tw != 0;
    nvs_close(h);
}

// Apply the in-memory config to the running system: break length + write-flag.
static void cfg_apply(void)
{
    s_break_secs   = s_cfg_break_min * 60;
    pomodoist_sync_set_write(s_cfg_tdwrite);   // gates the POST only; completions still record
}

static void seed_task(pomo_tasklist_t *l, int i, const char *title, const char *proj,
                      const char *tags, const char *desc, uint8_t pomos, uint8_t priority)
{
    snprintf(l->task[i].title, POMO_TITLE_LEN, "%s", title);
    snprintf(l->task[i].project, POMO_PROJ_LEN, "%s", proj);
    snprintf(l->task[i].tags, POMO_TAGS_LEN, "%s", tags);
    snprintf(l->task[i].desc, POMO_DESC_LEN, "%s", desc);
    l->task[i].pomos = pomos;
    l->task[i].priority = priority;
}
// The demo list is what every sim shot renders, so it has to carry the awkward cases and not
// just the flattering ones: a title at the 70-char mark the round face must wrap to two lines,
// a p1 that proves the rim takes its colour from priority, and a task planned at zero pomos
// so the day arithmetic is seen skipping it rather than counting it as one. Task 0's
// description is deliberately ~300 chars (D18): it overflows the detail card, so the `detail`
// shot proves the card scrolls rather than proving it fits.
static void seed_demo(void)
{
    EXT_RAM_BSS_ATTR static pomo_tasklist_t l;
    memset(&l, 0, sizeof l); l.count = 4;
    seed_task(&l, 0, "Draft the next chapter", "Field notes", "#writing #focus", "Start with the outline. Write one useful page, then take a break. This is fictional demonstration content.", 3, 4);
    seed_task(&l, 1, "Sketch a small idea", "Studio", "#creative", "Explore one shape on paper.", 1, 3);
    seed_task(&l, 2, "Read a few pages", "Learning", "#reading", "Make a note of one useful thought.", 0, 2);
    seed_task(&l, 3, "Water the plants", "Home", "#break", "A short screen-free pause.", 2, 1);
    pomo_core_set_tasks(&s_pc, &l);
}


// Rebuild the tag-chip row from a space-joined tags string ("#a #b"). Empty -> hidden.
static void rebuild_tags(const char *tags)
{
    if (!p_tagrow) return;
    lv_obj_clean(p_tagrow);
    if (!tags || !tags[0]) { lv_obj_add_flag(p_tagrow, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_clear_flag(p_tagrow, LV_OBJ_FLAG_HIDDEN);
    char buf[POMO_TAGS_LEN];
    lv_strlcpy(buf, tags, sizeof(buf));
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) tt_chip(p_tagrow, tok, TT_TAG_BG, TT_SLATE);
}

// DESIGN.md: eyebrows are uppercase with wide tracking. Todoist project names are whatever
// the user typed, and two faces render one, so the case fix lives in one place.
static void proj_upper(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

static const char s_puck_rail_tag = 0;   // user_data mark on a puck_rail bar; set_clock reads it

// Only touches the label when the rendered string actually changed. lv_label_set_text(_fmt)
// invalidates the label's region every call regardless of whether the text differs, and this
// runs every 100ms tick (focus/picker/launcher) - on the ST77916 panel that unconditional
// invalidation is a visible 10Hz shimmer even while the clock reads the same minute.
void pomodoist_rail_update(lv_obj_t *l)
{
    if (!l) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[48];   // generously sized against -Wformat-truncation's worst-case %d width, not "23:59"
    if (tm.tm_year + 1900 < 2020) snprintf(buf, sizeof buf, "--:--");  // not synced yet
    // A4, D8: a clock nobody has vouched for (kept across a reset, or read back from NVS) is
    // believable but not trusted, so it wears a "~".
    // The label is already the skin's muted role (TT_MUTED on paper), so the tilde is the whole
    // difference. Only BSP_CLOCK_NVS boards ever reach RESTORED; everywhere else this is the
    // plain branch below, byte for byte.
    else if (trev_net_time_state() == TREV_TIME_RESTORED) snprintf(buf, sizeof buf, "~%02d:%02d", tm.tm_hour, tm.tm_min);
    else snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    size_t k = strlen(buf);
    // Cached for 1s, for the same reason as the battery below and a sharper one: outbox_count
    // takes the outbox's recursive mutex, and outbox_pop_sent holds that mutex across NVS
    // commits while a flush drains. Asking at 10Hz would put the UI behind a flash write
    // repeatedly, on exactly the day (a long offline stretch flushing at once) the rail exists
    // to report on. The number only moves on a completion or a flush.
    static int s_pend;
    static uint32_t s_pend_at;
    static bool s_pend_read;
    if (!s_pend_read || lv_tick_elaps(s_pend_at) >= 1000) {
        s_pend_read = true;
        s_pend_at = lv_tick_get();
        s_pend = outbox_count();
    }
    if (s_pend > 0) k += (size_t)snprintf(buf + k, sizeof buf - k, " %s %d", TT_GLYPH_MIDDOT, s_pend);
    if (k >= sizeof buf) k = sizeof buf - 1;   // snprintf returns the UNtruncated length; an unclamped k wraps the next size arg
    (void)k;   // the segments accumulate through k; nothing downstream reads the final length
    // The puck's 152 px rail (puck_rail): the app name yields only when the clock plus its pending
    // and battery marks (~HH:MM · 3) would collide with it. Rails from rail_init hide the name always.
    lv_obj_t *rail = lv_obj_get_parent(l);
    if (rail && lv_obj_get_user_data(rail) == (void *)&s_puck_rail_tag) {
        lv_point_t sz;
        lv_text_get_size(&sz, buf, TT_F_STATUS, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_t *name = lv_obj_get_child(rail, 0);
        bool hide = sz.x > 70;
        if (name && hide != lv_obj_has_flag(name, LV_OBJ_FLAG_HIDDEN)) {
            if (hide) lv_obj_add_flag(name, LV_OBJ_FLAG_HIDDEN);
            else      lv_obj_clear_flag(name, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (strcmp(lv_label_get_text(l), buf) != 0) lv_label_set_text(l, buf);
}
static void update_clock(void) { pomodoist_rail_update(p_clock); }

// B7: give a round status bar's clock label room for the whole rail. tt_statusbar sizes it to
// its content and right-aligns it against the bar, so a fixed width plus a right text-align
// keeps the segments from walking left and right as the pending count gains a digit. Called
// by each round builder right after tt_statusbar, never per tick: lv_obj_set_width refreshes
// the style whether or not the value changed.
//
// On round the app name goes with it, unconditionally. The rail and the name share one 156px
// interior (a 178px chord at TT_ROUND_INSET, less TT_PAD each side); "POMODOIST" is 79px of
// that and a full rail is ~117px, measured against plex_mono_sb_13's 7.8px cell, so the two
// cannot both be drawn. DESIGN.md asks for "app name left, clock or round-state right" and
// this IS the round-state: the pending count and the battery percent appear nowhere else on
// the device, while the eyebrow directly below already names the app. Hiding it here rather
// than when the rail happens to be long is the point - a name that pops out when a pomo lands
// in the outbox and back when the flush drains it is a layout jump the user cannot attribute
// to anything, and "identical structure on every face" would become structure that changes on
// one face. Rectangular boards never call this and keep the name.
void pomodoist_rail_init(lv_obj_t *clk)
{
    if (!clk) return;
    lv_obj_set_width(clk, 150);
    lv_obj_set_style_text_align(clk, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *bar = lv_obj_get_parent(clk);
    lv_obj_t *name = bar ? lv_obj_get_child(bar, 0) : NULL;   // tt_statusbar builds the name first, then the clock
    if (name && name != clk) lv_obj_add_flag(name, LV_OBJ_FLAG_HIDDEN);
}
// The puck's rail (pomo-focus.dc.html): 152x20 at (104,30), name left, clock right, the skin's muted
// tone from tt_statusbar. Replaces rail_init on the puck's Pomodoist faces. The name stays until
// set_clock finds the clock too wide for both (a ~HH:MM with a pending count).
static void puck_rail(lv_obj_t *bar)
{
    if (!bar) return;
    lv_obj_set_size(bar, 152, 20);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 104, 30);
    lv_obj_set_user_data(bar, (void *)&s_puck_rail_tag);
    lv_obj_t *name = lv_obj_get_child(bar, 0);   // tt_statusbar builds the name first, then the clock
    lv_obj_t *clk  = lv_obj_get_child(bar, 1);
    if (name) lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    if (clk)  lv_obj_align(clk,  LV_ALIGN_RIGHT_MID, 0, 0);
}

// C12: on the round face this row states the DAY, not just the phase - how many pomos are
// still owed across today's undone tasks, and the wall time the last of them lands on if you
// keep going. Rectangular glass keeps the bare word: there is no width there to spend, and
// both boards' faces are meant to stay byte-identical.
//
// It is a function, not a block inside pomo_render, because it has TWO callers. The focus
// face renders only when the core goes dirty, so an idle timer never redraws - and the DONE
// figure is wall-clock, which means it would freeze at the time of the last draw while the
// status bar's own clock kept advancing beside it. pomo_on_tick calls this next to
// update_clock() so the two move together. That is the face you read BEFORE starting a block,
// so it is the case that matters most.
static void phase_row_refresh(void)
{
    if (!p_phase) return;
    char ph[48];   // worst case "FOCUS . 99 LEFT . DONE 23:59" with 2-byte middots
    if (s_setmode) {
        lv_strlcpy(ph, "SET", sizeof ph);
    } else {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        bool synced = (tm.tm_year + 1900 >= 2020);
        // Local midnight both ends, so a pomo logged this morning counts and yesterday's does
        // not. Unsynced, those bounds are meaningless, so nothing is counted done and the row
        // reads as a full day owed - wrong high, never wrong low.
        //
        // Recomputed on a minute boundary (or a task-list size change), NOT per call: this is
        // one NVS-backed ring walk PER TASK, up to 32 of them, and both callers can run at
        // 10Hz. The figure it feeds only ever moves in whole pomos, so a per-frame walk buys
        // nothing. Residual: a sync push that swaps tasks without changing the count leaves the
        // counts up to a minute stale. Acceptable for an estimate; not worth a dirty flag.
        static uint8_t done_today[POMO_MAX_TASKS];
        static int done_min = -1, done_n = -1;
        if (synced && (tm.tm_min != done_min || s_pc.tasks.count != done_n)) {
            done_min = tm.tm_min;
            done_n   = s_pc.tasks.count;
            memset(done_today, 0, sizeof done_today);
            struct tm mid = tm;
            mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
            int64_t day0 = (int64_t)mktime(&mid);
            for (int i = 0; i < s_pc.tasks.count; i++)
                done_today[i] = (uint8_t)outbox_done_today(s_pc.tasks.task[i].id, day0, day0 + 86400);
        }
        int owed = pomo_core_pomos_left(&s_pc, done_today);
        // Two digits is the row's whole width budget. 32 tasks x 8 pomos can nominally owe 256,
        // and "256 LEFT" is what pushes this string past the 280px measure: the one-digit form
        // measures 240px on the sim shots (8.9px per mono cell), so the two-digit form is ~249px
        // and the run dot's slot costs another 9 - 258 of 280, about two characters of headroom.
        // A day owed more than 99 pomos is not a day, so clamp the display rather than widen it.
        if (owed > 99) owed = 99;
        if (owed <= 0) {
            lv_strlcpy(ph, "FOCUS", sizeof ph);         // nothing planned: say the phase and stop
        } else if (!synced) {
            snprintf(ph, sizeof ph, "FOCUS %s %d LEFT", TT_GLYPH_MIDDOT, owed);
        } else {
            // 900 = the long break every 4th block, the one number the core cannot infer.
            time_t eta = now + (time_t)pomo_core_eta_s(owed, s_pc.total_s, s_break_secs, 900);
            struct tm et;
            localtime_r(&eta, &et);
            snprintf(ph, sizeof ph, "FOCUS %s %d LEFT %s DONE %02d:%02d",
                     TT_GLYPH_MIDDOT, owed, TT_GLYPH_MIDDOT, et.tm_hour, et.tm_min);
        }
    }
    // Same shimmer guard as set_clock, and it is what makes the once-a-tick caller free: a bare
    // lv_label_set_text invalidates the row whether or not the string differs, and this row is
    // wide enough for that to read as a flicker.
    if (strcmp(lv_label_get_text(p_phase), ph) != 0) lv_label_set_text(p_phase, ph);
}

static void pomo_render(void)
{
    if (s_view == PV_BREAK) {
        // pomo_on_tick calls pomo_render() every 100ms tick on PV_BREAK (not just once/sec),
        // so the countdown text is the same string on 9 of 10 calls - same unconditional-set
        // shimmer as Fix 1, just one function deeper. The countdown itself still updates once
        // a second (s_break_left); only the redundant same-string label write is skipped.
        if (p_break_clock) {
            // 24, not 8: s_break_left/60 and %60 are both actually 0..59, but the %u args
            // widen to `unsigned` at this call, so -Werror=format-truncation sizes against
            // the full 32-bit range (same false positive trevos_home_round.c's clock buffer
            // works around). 24 comfortably covers "%02u:%02u" worst case.
            char b[24];
            snprintf(b, sizeof b, "%02u:%02u", (unsigned)(s_break_left / 60), (unsigned)(s_break_left % 60));
            if (strcmp(lv_label_get_text(p_break_clock), b) != 0) lv_label_set_text(p_break_clock, b);
        }
        // The rim answers "how far" on every face; on break that is time left, in amber. Only
        // the round face creates this, so the null guard is what keeps the rectangular boards
        // byte-identical instead of needing a branch of their own.
        if (p_break_arc && s_break_secs) {
            // The puck's rim is what is LEFT (pomo-break.dc.html), draining from 12 o'clock.
            uint32_t left = s_break_left < s_break_secs ? s_break_left : s_break_secs;
            lv_arc_set_value(p_break_arc, (int32_t)((uint64_t)left * 1000 / s_break_secs));
        }
        return;
    }
    update_clock();                 // focus, picker and ledger share the wall clock / rail
    // Both are built from state once and never per-tick: the picker rebuilds on a turn, the
    // ledger on entry. Everything below this line is focus-face work.
    if (s_view == PV_PICKER || s_view == PV_LEDGER) return;
    // Set-mode (A3): the numerals show the editable block length (mm:00), the phase reads
    // "SET", the runs-dot hides, and the arc shows full so the dial reads as a length, not a countdown.
    uint32_t left = s_setmode ? s_pc.total_s : s_pc.left_s;
    if (p_time) lv_label_set_text_fmt(p_time, "%02u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
    if (p_arc) {
        // The puck's rim is what is LEFT, draining from 12 o'clock (pomo-focusrun.dc.html), and empty
        // while idle. Running, paused and just-started all read left / total.
        bool idle_now = !s_pc.running && s_pc.left_s == s_pc.total_s;
        int v = (idle_now || !s_pc.total_s) ? 0 : (int)((uint64_t)left * 1000 / s_pc.total_s);
        tt_arc_anim(p_arc, v, 600);  // buttery ease-out fill
    }
    phase_row_refresh();
    focus_hint_refresh();
    if (p_len) {   // LENGTH lives on the wheel: shown only while the timer is idle
        bool idle_now = !s_pc.running && s_pc.left_s == s_pc.total_s;
        lv_obj_t *row = lv_obj_get_parent(p_len);
        if (idle_now) {
            char lb[24];
            snprintf(lb, sizeof lb, "%u MIN", (unsigned)(s_pc.total_s / 60));
            if (strcmp(lv_label_get_text(p_len), lb) != 0) lv_label_set_text(p_len, lb);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // Hidden rather than transparent: on the round face the dot is laid out in the phase row,
    // and only a hidden child is skipped by flex - a transparent one still holds its slot and
    // leaves the idle row off-centre by its own width. Rectangular faces have no phase row to
    // shift, so the change is invisible there.
    if (p_rundot) {
        if (!s_setmode && s_pc.running) lv_obj_clear_flag(p_rundot, LV_OBJ_FLAG_HIDDEN);
        else                            lv_obj_add_flag(p_rundot, LV_OBJ_FLAG_HIDDEN);
    }
    const pomo_task_t *t = pomo_core_active_task(&s_pc);
    // C15: priority rides the rim. It is the one element big enough to read from across the
    // room, and a p1 that only announces itself in a list you have to open is not a warning.
    // Round only - the rectangular ring is a 52px stub where a colour swap reads as a glitch.
    // Cached, because lv_obj_set_style_arc_color refreshes the style and invalidates
    // unconditionally, and this object is the full 336x336 rim. Reset on a NULL arc so the
    // colour is re-applied to the fresh object after a face rebuild.
    static uint8_t last_pr = 0xFF;
    if (!p_arc) {
        last_pr = 0xFF;
    } else {
        uint8_t pr = t ? t->priority : 0;
        if (pr != last_pr) {
            last_pr = pr;
            lv_obj_set_style_arc_color(p_arc,
                pr == 4 ? TT_CORAL : pr == 3 ? TT_AMBER : pr == 2 ? TT_GREEN : tt_skin_paper()->accent,
                LV_PART_INDICATOR);
        }
    }
    if (p_eyebrow) {
        // No project: just "TODAY". The old fallback repeated the app name, which put
        // "POMODOIST" twice in a row under a status bar already saying it (seen on the
        // round board's glass 2026-08-29).
        if (t && t->project[0]) {
            char proj[POMO_PROJ_LEN];
            proj_upper(proj, sizeof proj, t->project);
            lv_label_set_text_fmt(p_eyebrow, "%s %s TODAY", proj, TT_GLYPH_MIDDOT);
        } else {
            lv_label_set_text(p_eyebrow, "TODAY");
        }
    }
    // "No tasks - sync" named an action this UI does not offer — there is no sync control
    // anywhere. State the fact instead; the timer still works without a task (D3).
    if (p_title)   lv_label_set_text(p_title, t ? t->title : "Nothing due today");
    rebuild_tags(t ? t->tags : NULL);
    if (p_descbox && p_desc) {
        if (t && t->desc[0]) { lv_obj_clear_flag(p_descbox, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(p_desc, t->desc); }
        else lv_obj_add_flag(p_descbox, LV_OBJ_FLAG_HIDDEN);
    }
}


static void build_focus_puck(lv_obj_t *root)
{
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(root, sk);
    puck_rail(tt_statusbar(root, "POMODOIST", sk, &p_clock));

    // rim r 168..176, ticks 2x8 straddling r=172 at 12, 3, 6 and 9. Paint order is track, ticks, coral
    // progress (as mocked): one lv_arc paints its track and indicator together, so the track is its own
    // arc under the ticks and p_arc, on top of them, shows only the indicator.
    lv_obj_t *track = lv_arc_create(root);
    lv_obj_set_size(track, POMO_RIM_D, POMO_RIM_D);
    lv_obj_center(track);
    lv_arc_set_rotation(track, 270);
    lv_arc_set_bg_angles(track, 0, 360);
    lv_obj_remove_style(track, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(track, POMO_RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(track, TT_RING_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(track, LV_OPA_TRANSP, LV_PART_INDICATOR);
    const int TK = POMO_RIM_D / 2 - POMO_RIM_W / 2;   // 172
    const struct { int dx, dy, w, h; } tk[] = {
        { 0, -TK, 2, 8 }, { 0, TK, 2, 8 }, { -TK, 0, 8, 2 }, { TK, 0, 8, 2 },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = lv_obj_create(root);
        lv_obj_remove_style_all(t);
        lv_obj_set_size(t, tk[i].w, tk[i].h);
        lv_obj_set_style_bg_color(t, TT_TICK, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_align(t, LV_ALIGN_CENTER, tk[i].dx, tk[i].dy);
    }

    p_arc = lv_arc_create(root);
    lv_obj_set_size(p_arc, POMO_RIM_D, POMO_RIM_D);
    lv_obj_center(p_arc);
    lv_arc_set_rotation(p_arc, 270);
    lv_arc_set_bg_angles(p_arc, 0, 360);
    lv_arc_set_range(p_arc, 0, 1000);
    lv_arc_set_value(p_arc, 0);   // idle: empty; pomo_render drains it from 12 o'clock while a block is live
    lv_obj_remove_style(p_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(p_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(p_arc, POMO_RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(p_arc, LV_OPA_TRANSP, LV_PART_MAIN);   // the track is the arc under the ticks
    lv_obj_set_style_arc_width(p_arc, POMO_RIM_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(p_arc, sk->accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(p_arc, true, LV_PART_INDICATOR);

    // eyebrow cy 68, title cy 110 (two lines), hero cy 186, meta cy 244
    p_eyebrow = tt_label(root, "POMODOIST", TT_F_LABEL, TT_DESC, 2);
    lv_obj_set_width(p_eyebrow, 280);
    lv_obj_set_height(p_eyebrow, 15);
    lv_label_set_long_mode(p_eyebrow, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(p_eyebrow, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(p_eyebrow, LV_ALIGN_TOP_MID, 0, 68 - 15 / 2);

    p_title = tt_title(root, "Wire Todoist OAuth", sk->ink);
    lv_obj_set_width(p_title, 264);
    lv_obj_set_height(p_title, 46);   // two whole plex_sans_b_22 lines, so LONG_DOT ellipsizes
    lv_label_set_long_mode(p_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(p_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(p_title, LV_ALIGN_TOP_MID, 0, 110 - 46 / 2);

    p_time = tt_numerals(root, sk->ink);
    lv_obj_set_style_text_font(p_time, TT_F_NUM, 0);
    lv_obj_update_layout(p_time);
    lv_obj_align(p_time, LV_ALIGN_TOP_MID, 0, 186 - lv_obj_get_height(p_time) / 2);

    lv_obj_t *meta = lv_obj_create(root);
    lv_obj_remove_style_all(meta);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(meta, 280, 15);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta, 6, 0);
    p_rundot = tt_dot(meta, TT_CORAL, true, 6);   // pomo_render hides it (flex skips it) unless running
    p_phase = tt_label(meta, "FOCUS", TT_F_LABEL, TT_DESC, 1);
    lv_obj_align(meta, LV_ALIGN_TOP_MID, 0, 244 - 15 / 2);

    p_tagrow = NULL;      // no tag row on the puck; rebuild_tags() returns on NULL
    p_descbox = NULL;
    p_desc = NULL;
    p_hint = tt_actionbar2(root, "TASKS", "START", sk);   // mode 2: the classifier's halves match the pills
    tt_readout(root, 328, TT_DESC, &p_len);
    lv_label_set_text(p_len, "25 MIN");

    lv_obj_t *rows[] = { p_eyebrow, p_title, meta };
    tt_face_enter(root, p_time, rows, 3);
}



static void build_focus(lv_obj_t *root) { build_focus_puck(root); }

// Swap the focus hint bar between the normal verbs and set-mode's edit hint (A3).
// In set-mode both gestures change the length, so the standard KEY/BOOT verb pattern
// doesn't fit — rewrite the bar's single centred label directly.
static void focus_hint_refresh(void)
{
    if (!p_hint) return;
    // Round pills are drawn per zone and an unlabelled zone gets no pill at all, so a child
    // index no longer tracks a zone. Address them by zone id instead (T4's tt_actionbar_set_verb).
    // The puck's two pills: LENGTH is on the wheel and the picker is TASKS (or RESET once a block
    // is under way and not running). Set-mode is unreachable here. Zone 0 is the primary verb.
    {
        bool idle = (!s_pc.running && s_pc.left_s == s_pc.total_s);
        bool paused = (!s_pc.running && !idle);
        tt_actionbar_set_verb(p_hint, -1, paused ? "RESET" : "TASKS");
        tt_actionbar_set_verb(p_hint,  0, s_pc.running ? "PAUSE" : (idle ? "START" : "RESUME"));
        return;
    }
    if (s_setmode) {
        tt_actionbar_set_verb(p_hint, -1, "-5");
        tt_actionbar_set_verb(p_hint,  0, "SET");
        tt_actionbar_set_verb(p_hint,  1, "+5");
    } else {
        bool idle = (!s_pc.running && s_pc.left_s == s_pc.total_s);
        tt_actionbar_set_verb(p_hint, -1, idle ? "LENGTH" : "RESET");
        tt_actionbar_set_verb(p_hint,  0, s_pc.running ? "PAUSE" : (idle ? "START" : "RESUME"));
        tt_actionbar_set_verb(p_hint,  1, "TASKS");
    }
}

// ---- picker face (03): centred slate card flanked by dashed prev/next, dot pagination ----
// A centred row of the task's project (mono, optional) and its tag chips, gap 8, vertical centre cy
// (pomo-picker.dc.html row 232..252, pomo-detail.dc.html row 144..164; the focus face no longer shows tags).
// Returns NULL when there is nothing to show.
static lv_obj_t *puck_tag_row(lv_obj_t *root, const char *proj, const char *tags, int cy)
{
    bool has_proj = proj && proj[0], has_tags = tags && tags[0];
    if (!has_proj && !has_tags) return NULL;
    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    if (has_proj) tt_label(row, proj, TT_F_LABEL, TT_DESC, 1);
    if (has_tags) {
        char tb[POMO_TAGS_LEN];
        lv_strlcpy(tb, tags, sizeof(tb));
        for (char *tok = strtok(tb, " "); tok; tok = strtok(NULL, " ")) tt_chip(row, tok, TT_RING_TRACK, TT_SLATE);
    }
    lv_obj_update_layout(row);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, cy - lv_obj_get_height(row) / 2);
    return row;
}

// ---- picker face, PUCK (pomo-picker.dc.html) ----------------------------------------------------
// The wheel pages the list, so the face is a decision surface: the task's pomo count is the hero
// (what the next blocks cost), then title, description, project and tags, BACK | PICK, and the
// position readout "N / M" where the wheel hint lives on the focus face. The rim is list position
// (1 of 4 = a quarter, as mocked). Rows are placed by their mocked centre (cy); see build_focus_puck.
static void build_picker_puck(lv_obj_t *root)
{
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(root, sk);
    puck_rail(tt_statusbar(root, "POMODOIST", sk, &p_clock));

    int cnt = s_pc.tasks.count;
    int n = cnt > 0 ? cnt : 1;
    int cur = s_pc.cursor % n;
    const pomo_task_t *pk = cnt > 0 ? &s_pc.tasks.task[cur] : NULL;

    // A LOCAL arc, never p_arc (see build_picker_round): pomo_render returns early on PV_PICKER.
    lv_obj_t *ring = lv_arc_create(root);
    lv_obj_set_size(ring, POMO_RIM_D, POMO_RIM_D);
    lv_obj_center(ring);
    lv_arc_set_rotation(ring, 270);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_range(ring, 0, n);
    lv_arc_set_value(ring, cur + 1);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring, POMO_RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, TT_RING_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, POMO_RIM_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, sk->accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);

    // hero cy 94: the digit (TT_F_NUM has digits only) and POMOS, gap 10, bottoms aligned
    lv_obj_t *hero = lv_obj_create(root);
    lv_obj_remove_style_all(hero);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hero, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(hero, 10, 0);
    lv_obj_t *num = tt_label(hero, "", TT_F_NUM, sk->ink, 0);
    lv_label_set_text_fmt(num, "%d", pk ? pk->pomos : 0);
    tt_label(hero, "POMOS", TT_F_LABEL, TT_DESC, 1);
    lv_obj_update_layout(hero);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 94 - lv_obj_get_height(hero) / 2);

    // title cy 170 (two whole plex_sans_b_22 lines, so LONG_DOT ellipsizes), description cy 214
    lv_obj_t *ttl = tt_title(root, pk ? pk->title : "Nothing due today", sk->ink);
    lv_obj_set_width(ttl, 264);
    lv_obj_set_height(ttl, 46);
    lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ttl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 170 - 46 / 2);

    lv_obj_t *dsc = tt_label(root, pk ? pk->desc : "", TT_F_BODY, TT_DESC, 0);
    lv_obj_set_width(dsc, 280);
    lv_obj_set_height(dsc, 15);   // one plex_sans_r_14 line
    lv_label_set_long_mode(dsc, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(dsc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dsc, LV_ALIGN_TOP_MID, 0, 214 - 15 / 2);
    if (!(pk && pk->desc[0])) lv_obj_add_flag(dsc, LV_OBJ_FLAG_HIDDEN);   // hidden, not absent: nothing reflows

    char pj[POMO_PROJ_LEN] = "";
    if (pk && pk->project[0]) proj_upper(pj, sizeof pj, pk->project);
    lv_obj_t *trow = puck_tag_row(root, pj, pk ? pk->tags : NULL, 242);

    tt_actionbar2(root, "BACK", "PICK", sk);
    lv_obj_t *pos;
    tt_readout(root, 328, TT_DESC, &pos);
    lv_label_set_text_fmt(pos, "%d / %d", cnt ? cur + 1 : 0, cnt);

    lv_obj_t *rows[] = { ttl, dsc, trow };
    tt_face_enter(root, hero, rows, 3);
}

// ---- picker face, ROUND (02r) -------------------------------------------------------------
// The rim answers "how far" on every face. On focus that is progress through the block; here it
// is position in the list, so one glance means the same kind of thing everywhere. The landscape
// three-card row is dropped: side cards existed to imply a list on a wide panel, and on a circle
// they cost the centre its width while the rim states position exactly.



static void build_picker(lv_obj_t *root) { build_picker_puck(root); }





// ---- break face (04): carbon + amber, countdown in the status bar, image slot ----
// Round glass (ADR-0012): the break face is the one two-column face, and at 360x360 its two
// 154px-tall panels are the tallest edge-anchored things TrevOS draws. A box that tall has its
// corners 77px off the centre line, so it can only reach x = sqrt(R^2 - 77^2) = 162 from centre
// on a 180px-radius circle, not the full 180. The rectangular margins (8 and 10) put the
// corners at 188 and are clipped by the bezel; 20 puts them at 160 and clears it.
#define BRK_SIDE_L 20
#define BRK_SIDE_R 20
#define BRK_SKIP_LINE lv_color_hex(0x3A3830)

static void build_break_puck(lv_obj_t *root)
{
    const tt_skin_t *sk = tt_skin_carbon();
    tt_face_ground(root, sk);
    s_break_prev = 0; s_break_acc = 0;
    puck_rail(tt_statusbar(root, "POMODOIST", sk, &p_clock));   // carbon's muted tone is TT_CARBON_MU

    p_break_arc = lv_arc_create(root);
    lv_obj_set_size(p_break_arc, POMO_RIM_D, POMO_RIM_D);
    lv_obj_center(p_break_arc);
    lv_arc_set_rotation(p_break_arc, 270);
    lv_arc_set_bg_angles(p_break_arc, 0, 360);
    lv_arc_set_range(p_break_arc, 0, 1000);
    lv_arc_set_value(p_break_arc, 1000);
    lv_obj_remove_style(p_break_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(p_break_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(p_break_arc, POMO_RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(p_break_arc, TT_CARBON_MU, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(p_break_arc, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_arc_width(p_break_arc, POMO_RIM_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(p_break_arc, TT_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(p_break_arc, true, LV_PART_INDICATOR);

    // STEP AWAY cy 112, hero cy 180, BREATHE cy 240
    lv_obj_t *eb = tt_label(root, "STEP AWAY", TT_F_LABEL, TT_CARBON_TX, 2);
    lv_obj_align(eb, LV_ALIGN_TOP_MID, 0, 112 - 15 / 2);

    p_break_clock = tt_label(root, "05:00", TT_F_NUM, TT_AMBER, 0);
    lv_obj_update_layout(p_break_clock);
    lv_obj_align(p_break_clock, LV_ALIGN_TOP_MID, 0, 180 - lv_obj_get_height(p_break_clock) / 2);

    lv_obj_t *brow = lv_obj_create(root);
    lv_obj_remove_style_all(brow);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(brow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brow, 8, 0);
    tt_anim_breathe(tt_dot(brow, TT_AMBER, true, 8), 135, 1600);
    tt_label(brow, "BREATHE", TT_F_LABEL, TT_CARBON_MU, 1);
    lv_obj_update_layout(brow);
    lv_obj_align(brow, LV_ALIGN_TOP_MID, 0, 240 - lv_obj_get_height(brow) / 2);

    // The lone SKIP: an amber outline 96x44 at (132,268), the tt_actionbar2 lone pill's place, and mode 1
    // so the classifier's one zone (the whole bottom band) matches it. Drawn directly: tt_actionbar2's
    // lone pill is the filled primary, and the mockup's SKIP is an outline.
    lv_obj_t *skip = tt_pill(root, "SKIP", TT_CARBON, TT_AMBER, TT_AMBER, false, 1);
    lv_obj_set_size(skip, TT_BAR2_W, TT_BAR2_H);
    lv_obj_set_style_text_letter_space(lv_obj_get_child(skip, 0), 2, 0);
    lv_obj_set_pos(skip, tt_bar2_x(360, 0, true), tt_bar2_top(360));
    tt_actionbar_mode(1);

    update_clock();   // the rail paints now; pomo_on_tick keeps it moving
    lv_obj_t *rows[] = { eb, brow };
    tt_face_enter(root, p_break_clock, rows, 2);
}




static void build_break(lv_obj_t *root) { build_break_puck(root); }

// ---- ledger face, ROUND (C11) -------------------------------------------------------------
// One thesis: what did today actually contain. The focus face answers "how much is left",
// which is a promise; this face answers "what got done", which is a record - and it is the
// only surface that reads the durable outbox rather than the live core, so it stays true
// through an offline day and through a reboot. Reached by swiping down from focus or picker,
// left by swiping up or tapping BACK; long-press-anywhere is still HOME, as everywhere.
//
// Deliberately not scrollable and deliberately capped at six rows. A day with more than six
// distinct tasks in it is not a day this face can usefully summarise at desk distance, and a
// scroll gesture on a face whose only other gestures are swipes would fight them.
#define LEDGER_ROWS 6

typedef struct {
    struct { char id[POMO_ID_LEN]; uint16_t n; } row[LEDGER_ROWS];
    int nrows;
    int mins;
} ledger_acc_t;

// Runs inside outbox_for_each_today, which holds the outbox lock: read-only, and it never
// calls back into the outbox (pomodoist_outbox.h's contract - a callback that mutates
// corrupts the walk it is being handed).
static void ledger_collect(const outbox_item_t *it, bool sent, void *ctx)
{
    (void)sent;
    ledger_acc_t *a = (ledger_acc_t *)ctx;
    // A flushed pomo keeps only its id and end time in the sent ring, so its length is
    // counted at the CURRENT block length. Exact on a device whose block length rarely
    // changes, an estimate otherwise, which is all "MIN FOCUS" ever claims to be.
    a->mins += it->minutes ? it->minutes : (int)(s_pc.total_s / 60);
    for (int i = 0; i < a->nrows; i++)
        if (strcmp(a->row[i].id, it->task_id) == 0) { a->row[i].n++; return; }
    if (a->nrows == LEDGER_ROWS) return;   // a seventh task still counts toward MIN FOCUS; it just gets no row
    lv_strlcpy(a->row[a->nrows].id, it->task_id, sizeof a->row[a->nrows].id);
    a->row[a->nrows].n = 1;
    a->nrows++;
}

static void build_ledger_round(lv_obj_t *root)
{
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(root, sk);
    tt_statusbar(root, "POMODOIST", sk, &p_clock);
    pomodoist_rail_init(p_clock);

    // Same running-sum rhythm as build_focus_round, at DESIGN.md's 8px step: header 84..99,
    // then rows on a 23px pitch (a plex_sans_r_14 line is 15), then the footer 8 below the
    // last row. Six rows put the footer at 245..260 and the action bar's top edge is at 290,
    // so the fullest possible day still keeps 30px of daylight. A 260px measure is well
    // inside the chord at every one of those rows.
    const int MEASURE = 260;
    const int HEAD_Y = 84, HEAD_H = 15;
    const int ROW0_Y = HEAD_Y + HEAD_H + 8;   // 107
    const int ROW_H = 15, ROW_PITCH = ROW_H + 8;

    char head[48];
    time_t ls = 0;
    struct tm lt;
    if (pomodoist_sync_last_sync(&ls)) {
        localtime_r(&ls, &lt);
        snprintf(head, sizeof head, "LEDGER %s SYNCED %02d:%02d", TT_GLYPH_MIDDOT, lt.tm_hour, lt.tm_min);
    } else {
        snprintf(head, sizeof head, "LEDGER %s SYNCED NEVER", TT_GLYPH_MIDDOT);
    }
    lv_obj_t *hd = tt_eyebrow(root, head, sk->muted);
    lv_obj_set_width(hd, MEASURE);
    lv_obj_set_height(hd, HEAD_H);
    lv_label_set_long_mode(hd, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(hd, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hd, LV_ALIGN_TOP_MID, 0, HEAD_Y);

    lv_obj_t *rows[LEDGER_ROWS + 1] = { 0 };
    int nrows = 0;
    lv_obj_t *foot = NULL;

    static ledger_acc_t acc;   // static: ~200 bytes, and the LVGL task stack is not generous
    memset(&acc, 0, sizeof acc);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 >= 2020) {
        // Local midnight both ends, the same bounds the phase row's DONE estimate uses: an
        // unsynced clock makes them meaningless, so an unsynced disk shows an empty day
        // rather than a day starting at some 1970 boundary.
        struct tm mid = tm;
        mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
        int64_t day0 = (int64_t)mktime(&mid);
        outbox_for_each_today(day0, day0 + 86400, ledger_collect, &acc);
    }
    for (int i = 0; i < acc.nrows; i++) {
        const char *title = NULL;
        for (int t = 0; t < s_pc.tasks.count; t++)
            if (strcmp(s_pc.tasks.task[t].id, acc.row[i].id) == 0) { title = s_pc.tasks.task[t].title; break; }
        char line[POMO_TITLE_LEN + 24];
        if (title) {
            // "x", not the multiplication sign: plex_sans_r_14 carries ASCII plus U+00B7 and
            // nothing else, so a U+00D7 here would draw as a missing glyph.
            snprintf(line, sizeof line, "%u x %s", (unsigned)acc.row[i].n, title);
        } else {
            // A task completed today and since dropped from the list still earned its pomos.
            // The last four characters of the Todoist id are enough to tell two of them apart.
            size_t idn = strlen(acc.row[i].id);
            snprintf(line, sizeof line, "%u x task %s", (unsigned)acc.row[i].n,
                     acc.row[i].id + (idn > 4 ? idn - 4 : 0));
        }
        lv_obj_t *r = tt_label(root, line, TT_F_BODY, sk->ink, 0);
        lv_obj_set_width(r, MEASURE);
        lv_obj_set_height(r, ROW_H);   // one whole line, so LONG_DOT actually ellipsizes (see build_focus_round)
        lv_label_set_long_mode(r, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(r, LV_ALIGN_TOP_MID, 0, ROW0_Y + i * ROW_PITCH);
        rows[nrows++] = r;
    }
    if (acc.nrows) {
        char ft[48];
        snprintf(ft, sizeof ft, "%d MIN FOCUS %s %d PENDING", acc.mins, TT_GLYPH_MIDDOT, outbox_count());
        foot = tt_label(root, ft, TT_F_LABEL, sk->muted, TT_TRACK_WIDE);
        lv_obj_align(foot, LV_ALIGN_TOP_MID, 0, ROW0_Y + acc.nrows * ROW_PITCH);
        // Deliberately NOT appended to rows[]: it is this face's hero (see tt_face_enter
        // below), and DESIGN.md's entrance recipe gives the hero a scale-in and the rows a
        // staggered fade. Passing it as both would run both on one label.
    }

    if (!nrows) {
        // An empty day states itself in one line at the optical centre. Nothing is dimmed or
        // greyed into a fake row: the face has no content and says so.
        lv_obj_t *none = tt_label(root, "No pomos yet today", TT_F_BODY, sk->muted, 0);
        lv_obj_align(none, LV_ALIGN_CENTER, 0, 0);
        rows[nrows++] = none;
    }

    // A local, never p_hint: focus_hint_refresh() relabels p_hint with the focus verbs, and
    // this bar has one verb that never changes.
    tt_actionbar(root, "", "BACK", "", sk);

    tt_face_enter(root, foot ? foot : hd, rows, nrows);
}

// ---- the detail card (D18) -----------------------------------------------------------------
// Design requirement: "when a task has a long comment that still gets cut off, build it so I
// can click into it and a toast pop up comes up which has the full, scrollable text."
//
// It is an overlay, not a view. A pomo_view_t would mean lv_obj_clean + rebuild on the way in
// AND on the way out, which throws away the focus face's live arc and clock for a card that is
// meant to sit on top of them for a few seconds. A child object costs one delete to dismiss and
// leaves the face underneath exactly as it was, mid-block included.
//
// Sizing is a CORNER problem, not a chord problem, and getting that wrong is what made the
// first cut of this card too narrow to read. The only constraint is that the rectangle's
// corner stays inside the safe radius:
//
//     sqrt((W/2)^2 + (H/2)^2) <= 168
//
// which is a budget shared between width and height. Clamping the width to the chord at the
// card's top row spends almost all of it on height: 201 wide by 264 tall left a 169px text
// measure, about 24 characters a line. 250 x 220 spends it the other way and gives a 218px
// measure, about 32 characters, which is a readable line. Corner check:
// sqrt(125^2 + 110^2) = 166.5, so there is ~1.5px of daylight at 168.
//
// DO NOT widen either number without redoing that arithmetic. W and H trade against each
// other; growing one means shrinking the other, and roundsafe.py is the check.
#define DETAIL_W 250
#define DETAIL_H 220
static lv_obj_t *s_detail_prev_clock;   // the face's rail clock, parked while the puck's card carries its own
static void detail_close(void)
{
    if (!p_detail) return;
    p_clock = s_detail_prev_clock;   // the face's own rail takes the tick back
    ESP_LOGI(TAG, "detail close");
    lv_obj_delete(p_detail);
    p_detail = NULL;
}

static void detail_open_puck(const pomo_task_t *t)
{
    const tt_skin_t *sk = tt_skin_paper();
    p_detail = lv_obj_create(s_pomo_root);
    lv_obj_remove_style_all(p_detail);
    lv_obj_clear_flag(p_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p_detail, 360, 360);
    lv_obj_set_pos(p_detail, 0, 0);
    lv_obj_set_style_bg_color(p_detail, TT_PAPER_ALT, 0);
    lv_obj_set_style_bg_opa(p_detail, LV_OPA_COVER, 0);   // fully opaque, and the same colour in the four corners

    s_detail_prev_clock = p_clock;
    lv_obj_t *clk = NULL;
    puck_rail(tt_statusbar(p_detail, "POMODOIST", sk, &clk));
    p_clock = clk;   // update_clock keeps the card's rail ticking; detail_close hands it back

    // eyebrow cy 68, title cy 110, tags cy 154, description top 174
    char proj[POMO_PROJ_LEN];
    if (t->project[0]) proj_upper(proj, sizeof proj, t->project);
    else               lv_strlcpy(proj, "TODAY", sizeof proj);
    lv_obj_t *eb = tt_label(p_detail, proj, TT_F_LABEL, TT_DESC, 1);
    lv_obj_set_width(eb, 280);
    lv_obj_set_height(eb, 15);
    lv_label_set_long_mode(eb, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(eb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(eb, LV_ALIGN_TOP_MID, 0, 68 - 15 / 2);

    lv_obj_t *ti = tt_title(p_detail, t->title, sk->ink);
    lv_obj_set_width(ti, 256);
    lv_obj_set_height(ti, 46);   // two whole plex_sans_b_22 lines
    lv_label_set_long_mode(ti, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ti, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 110 - 46 / 2);

    puck_tag_row(p_detail, NULL, t->tags, 154);

    // Four plex_sans_r_14 lines: 15 px glyph height, 5 px line space (the mocked 20 px pitch), so
    // 4 * 15 + 3 * 5 = 75 px, y 174..249. That is 19 px above the pills (268): never clipped, never in
    // their way. LONG_DOT cuts the fifth line's worth of text with an ellipsis on the fourth.
    lv_obj_t *de = tt_label(p_detail, t->desc[0] ? t->desc : "No description", TT_F_BODY, TT_DESC, 0);
    lv_obj_set_width(de, 272);
    lv_obj_set_style_text_line_space(de, 5, 0);
    lv_obj_set_height(de, 75);
    lv_label_set_long_mode(de, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(de, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(de, LV_ALIGN_TOP_MID, 0, 174);

    // The right verb is the face underneath's own primary verb, derived, never stored.
    const char *right = s_view == PV_PICKER ? "PICK"
                      : s_pc.running ? "PAUSE"
                      : (s_pc.left_s == s_pc.total_s ? "START" : "RESUME");
    tt_actionbar2(p_detail, "BACK", right, sk);
    tt_anim_fade_in(p_detail, 160, 0);
}

static void detail_open(const pomo_task_t *t)
{
    if (!s_pomo_root || p_detail || !t) return;   // no task, no card: an empty list has nothing to show
    detail_open_puck(t);
    return;
    const tt_skin_t *sk = tt_skin_paper();
    const int PAD = 16;
    const int TEXT_W = DETAIL_W - 2 * PAD;

    p_detail = lv_obj_create(s_pomo_root);
    lv_obj_set_size(p_detail, DETAIL_W, DETAIL_H);
    lv_obj_center(p_detail);
    lv_obj_set_style_bg_color(p_detail, sk->ground, 0);
    lv_obj_set_style_bg_opa(p_detail, LV_OPA_COVER, 0);   // fully opaque: the face behind must not read through
    lv_obj_set_style_border_color(p_detail, sk->rule, 0);
    lv_obj_set_style_border_width(p_detail, 1, 0);
    lv_obj_set_style_border_opa(p_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_detail, 16, 0);
    lv_obj_set_style_pad_all(p_detail, PAD, 0);
    lv_obj_set_style_pad_row(p_detail, 8, 0);
    lv_obj_set_flex_flow(p_detail, LV_FLEX_FLOW_COLUMN);
    // Vertical only, and no scrollbar: the bar would be one more thing drawn near the rim, and
    // the swipe that scrolls this is the same swipe that pages tasks everywhere else.
    lv_obj_add_flag(p_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p_detail, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p_detail, LV_SCROLLBAR_MODE_OFF);

    char proj[POMO_PROJ_LEN];
    if (t->project[0]) proj_upper(proj, sizeof proj, t->project);
    else               lv_strlcpy(proj, "TODAY", sizeof proj);
    lv_obj_t *eb = tt_eyebrow(p_detail, proj, sk->muted);
    lv_obj_set_width(eb, TEXT_W);
    lv_label_set_long_mode(eb, LV_LABEL_LONG_DOT);

    lv_obj_t *ti = tt_title(p_detail, t->title, sk->ink);
    lv_obj_set_width(ti, TEXT_W);
    lv_label_set_long_mode(ti, LV_LABEL_LONG_WRAP);   // the whole point: nothing is cut off in here

    // One body label rather than the focus face's chip row. Chips are a fixed-height flex row
    // that would have to wrap inside an already-narrow card; the tags string is already the
    // space-joined "#a #b" the chips are built from, so printing it is the same information.
    if (t->tags[0]) {
        lv_obj_t *tg = tt_label(p_detail, t->tags, TT_F_BODY, sk->muted, 0);
        lv_obj_set_width(tg, TEXT_W);
        lv_label_set_long_mode(tg, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t *de = tt_label(p_detail, t->desc[0] ? t->desc : "No description", TT_F_BODY,
                            t->desc[0] ? TT_DESC : sk->muted, 0);
    lv_obj_set_width(de, TEXT_W);
    lv_label_set_long_mode(de, LV_LABEL_LONG_WRAP);

    tt_anim_fade_in(p_detail, 160, 0);
}

static void pomo_build(lv_obj_t *root)
{
    switch (s_view) {
        case PV_PICKER: build_picker(root); break;
        case PV_BREAK:  build_break(root);  break;
        case PV_LEDGER: build_ledger_round(root); break;
        default:        build_focus(root);  break;   // PV_LEDGER off round: unreachable, and focus is the safe landing
    }
}

// Switch the active pomodist view: drop stale focus pointers, rebuild the root, render.
static void set_view(pomo_view_t v)
{
    if (!s_pomo_root) return;
                        // tear down the break loop timer if it was running
    s_setmode = false;              // set-mode is a focus-only sub-state; never survives a view switch
    // The face-down flag can only live as long as the break face it describes. FACE_UP used to
    // be its one exit, so a BACK tap or an expired break left it set and a later face-up fired
    // a toggle that paired with no face-down, pausing a block the user had restarted by hand.
    if (v != PV_BREAK) s_face_break = false;
    s_view = v;
    p_clock = p_arc = p_time = p_rundot = p_eyebrow = p_title = p_tagrow = p_descbox = p_desc = p_break_clock = NULL;
    p_phase = p_hint = NULL;
    p_len = NULL;
    // The ledger draws no tt_actionbar and a face swap inside the app skips trev_open's reset, so it must not
    // inherit the previous face's halves. Focus, picker and break each select their own mode as they build.
    if (v != PV_FOCUS) tt_actionbar_mode(3);
    p_detail = NULL;   // D18: the clean below frees the card with everything else; drop the pointer with them
    lv_obj_clean(s_pomo_root);
    pomo_build(s_pomo_root);
    pomo_render();
    tt_anim_root_fade(s_pomo_root, 200);   // C5: cross-fade the rebuilt face so it doesn't snap
}

static void pomo_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    s_pomo_root = root;
    // 10 = the ledger, and it is a ROUND dev flag: off round pomo_build falls back to the
    // focus face, which is the honest thing for a board that does not have this face.
    s_view = (TT_DEV_VIEW == 2) ? PV_PICKER : (TT_DEV_VIEW == 3) ? PV_BREAK
           : (TT_DEV_VIEW == 10) ? PV_LEDGER : PV_FOCUS;
    s_setmode = false;   // relaunch (BOOT-long home -> reopen) bypasses set_view, so clear set-mode here too
    pomo_build(root);
    pomo_render();
}

static void pomo_on_stop(trev_app_t *a)
{
    (void)a;
    
    s_pomo_root = NULL;
    p_clock = p_arc = p_time = p_rundot = p_eyebrow = p_title = p_tagrow = p_descbox = p_desc = p_break_clock = NULL;
    p_phase = p_hint = NULL;
    p_len = NULL;
    p_detail = NULL;
}
// Step the set-mode length by +/-5, wrapping 60->5 (up) and 5->60 (down). Direct total_s
// set because pomo_core_nudge_minutes clamps (doesn't wrap) and set-mode wants a full cycle.
static void setmode_step(int dir)
{
    int m = (int)(s_pc.total_s / 60) + dir * 5;
    if (m > POMO_MAX_MINUTES) m = POMO_MIN_MINUTES;        // 60 -> 5
    else if (m < POMO_MIN_MINUTES) m = POMO_MAX_MINUTES;  // 5  -> 60
    s_pc.total_s = (uint32_t)m * 60;
    s_pc.left_s  = s_pc.total_s;   // editing length resets the countdown (block is idle)
    s_pc.dirty   = true;
}

static void pomo_on_turn(trev_app_t *a, trev_turn_t d)
{
    (void)a;
    // D18: a flank pill dismisses the card, same as the mid pill in pomo_on_commit. Glass
    // 2026-09-05: with the card open on the picker, NEXT rebuilt the picker underneath it (the
    // direct clean below), freed the card, and the next content tap deleted it twice
    // (LoadProhibited in detail_close). Closing here keeps every rebuild path card-free.
    if (p_detail) { detail_close(); return; }
    if (s_view == PV_FOCUS) {   // the left pill (zone -1); a detent is pomo_on_wheel, a right-zone tap is COMMIT
        if (d == TREV_TURN_NEXT) return;   // two-verb bar: no right zone
        if (!s_pc.running && s_pc.left_s != s_pc.total_s) {   // paused: RESET
            pomo_core_reset(&s_pc);
            pomo_persist_save();
            pomo_render(); s_pc.dirty = false;
        } else {
            s_pc.cursor = s_pc.active; set_view(PV_PICKER);   // TASKS
        }
        return;
    }
    if (s_view == PV_PICKER) {   // the left pill is BACK (nothing selected); tasks page on the wheel (pomo_on_wheel)
        if (d == TREV_TURN_PREV) set_view(PV_FOCUS);
        return;
    }
    if (s_view == PV_FOCUS) {
        if (s_setmode) {
            setmode_step(d == TREV_TURN_NEXT ? +1 : -1);   // KEY +5 / KEY-long -5, wrapping
        } else if (d == TREV_TURN_NEXT) {
            s_pc.cursor = s_pc.active; set_view(PV_PICKER); // KEY -> task picker
        } else if (d == TREV_TURN_PREV && (s_pc.running || s_pc.left_s != s_pc.total_s)) {
            pomo_core_reset(&s_pc);
            pomo_persist_save();
        } else if (!s_pc.running && s_pc.left_s == s_pc.total_s) {
            s_setmode = true; s_pc.dirty = true;            // KEY-long while idle -> enter set-mode
        }
    } else if (s_view == PV_PICKER) {
        pomo_core_cursor_move(&s_pc, d == TREV_TURN_NEXT ? 1 : -1);                     // swipe tasks
        p_detail = NULL;   // this clean frees the card too; the pointer must go with it (see set_view)
        lv_obj_clean(s_pomo_root); build_picker(s_pomo_root);
        pomo_render();
        s_pc.dirty = false;
    }
    if (s_pc.dirty && s_view == PV_FOCUS) { pomo_render(); s_pc.dirty = false; }
}
static void pomo_on_commit(trev_app_t *a)
{
    (void)a;
    // The puck's card carries the face's own primary verb on its right pill (detail_open_puck), so a
    // commit closes the card and then runs that verb below; BACK is the left pill (pomo_on_turn).
    if (p_detail) detail_close();
    if (s_view == PV_PICKER) { pomo_core_select(&s_pc, s_pc.cursor); set_view(PV_FOCUS); pomo_persist_save(); }  // task selected (A2)
    else if (s_view == PV_BREAK) { s_break_left = s_break_secs; set_view(PV_FOCUS); }
    // The ledger's one verb is BACK. Without this branch a centre tap there would fall through
    // to the toggle below and silently start or pause the timer on a face that shows neither.
    else if (s_view == PV_LEDGER) { set_view(PV_FOCUS); }
    else if (s_setmode) { s_setmode = false; pomo_render(); pomo_persist_save(); }  // confirm new length, exit set-mode (A3 + A2)
    else {   // focus: start / pause (A2)
        bool starting = !s_pc.running && s_pc.left_s == s_pc.total_s;
        pomo_core_toggle(&s_pc);
        if (starting && s_pc.running) ESP_LOGI(TAG, "block start");
        pomo_render(); pomo_persist_save();
    }
}
// A detent (or a sideways swipe: on a wheel board a swipe means a detent). On the focus face it
// nudges the length, which the core allows only while idle; the bool is "felt", so the haptic ticks
// only when the length really moved (false at 5 or 60, while running or paused, and under the card).
// No NVS write per detent: one save 1 s after the last felt nudge (#93), the same idle shape Settings
// uses; START, RESET and the T3's set-mode confirm also persist. The picker pages its cursor, felt only
// when it moved.
#define POMO_LEN_SAVE_MS 1000
static lv_timer_t *s_len_save;
static void pomo_len_save_cb(lv_timer_t *t)
{
    (void)t;
    s_len_save = NULL;   // one-shot: the repeat count deletes the timer after this
    pomo_persist_save();
    ESP_LOGI(TAG, "pomo: length %u min saved", (unsigned)(s_pc.total_s / 60));
}
// The LENGTH readout's slide (Task 5.12.5a): the row carries the glyph and the label, and only its
// translate_x moves (layer-free, nothing else). 8 px along the bezel, 120 ms ease-out, from the side
// the rim moves at 6 o'clock: a longer block enters from the right, a shorter from the left.
#define LEN_SLIDE_PX 8
#define LEN_SLIDE_MS 120
static void len_tx_exec(void *var, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0); }
static bool pomo_on_wheel(trev_app_t *a, trev_turn_t d)
{
    if (p_detail) return false;
    if (s_view == PV_FOCUS) {
        uint32_t before = s_pc.total_s;
        pomo_core_nudge_minutes(&s_pc, d == TREV_TURN_NEXT ? 5 : -5);
#ifndef ESP_PLATFORM
        fprintf(stderr, "[pomo] wheel len=%u\n", (unsigned)(s_pc.total_s / 60));
#endif
        if (s_pc.total_s == before) return false;
        if (s_len_save) lv_timer_reset(s_len_save);
        else { s_len_save = lv_timer_create(pomo_len_save_cb, POMO_LEN_SAVE_MS, NULL); lv_timer_set_repeat_count(s_len_save, 1); }
        pomo_render(); s_pc.dirty = false;
        if (p_len) {   // pomo_render has swapped the texts (numerals never tween); the readout slides in from its side
            lv_obj_t *row = lv_obj_get_parent(p_len);
            int32_t from = LV_CLAMP(-LEN_SLIDE_PX, tt_step_live(row, len_tx_exec, 0) + (d == TREV_TURN_NEXT ? LEN_SLIDE_PX : -LEN_SLIDE_PX), LEN_SLIDE_PX);
            tt_step(row, len_tx_exec, from, 0, LEN_SLIDE_MS, NULL);
        }
        return true;
    }
    if (s_view == PV_PICKER) {
        // One task per detent, and no wrap: at either end there is nothing to page to, so no rebuild and
        // the false means no tick (D6). pomo_core_cursor_move itself wraps, so the bound is checked here.
        int step = d == TREV_TURN_NEXT ? 1 : -1;
        int to = s_pc.cursor + step;
        if (to < 0 || to >= s_pc.tasks.count) return false;
        pomo_core_cursor_move(&s_pc, step);
        lv_obj_clean(s_pomo_root); build_picker(s_pomo_root);
        pomo_render();
        s_pc.dirty = false;
        return true;
    }
    return false;
}

// Per-app tick: runs ONLY while pomodist is the active face. The core countdown is NOT
// advanced here (that moved to the always-on background timer, A1, so the block keeps
// running off-face). This handles only what is face-local: pulling a fresh task list on
// the focus face, and running the break countdown.
static void pomo_on_tick(trev_app_t *a, uint32_t now)
{
    (void)a;
#if !TT_DEV_DEMO
    if (s_view == PV_FOCUS) {
        EXT_RAM_BSS_ATTR static pomo_tasklist_t pushed;   // static: 9.7 KB at 120, 21.2 KB at 480 on the disk, off the LVGL task stack (single-threaded use)
        if (pomodoist_sync_take(&pushed)) { pomo_core_set_tasks(&s_pc, &pushed); pomo_persist_save(); }
    }
#endif
    if (s_view == PV_BREAK) {                          // run the break countdown; auto-return to focus
        if (s_break_prev == 0) { s_break_prev = now; return; }
        s_break_acc += now - s_break_prev;
        s_break_prev = now;
        while (s_break_acc >= 1000 && s_break_left > 0) { s_break_acc -= 1000; s_break_left--; }
        if (s_break_left == 0) { set_view(PV_FOCUS); return; }
        pomo_render();
        update_clock();   // the puck's break carries the wall clock in its rail; pomo_render returns before it on PV_BREAK
        return;
    }
    // The ledger keeps a live rail too: its pending count is the number that moves while you
    // are looking at it, and a stalled rail on a face about syncing would read as a fault.
    if (s_view == PV_FOCUS || s_view == PV_PICKER || s_view == PV_LEDGER) update_clock();
    phase_row_refresh();   // wall-clock row, so it tracks the clock and not the dirty flag
}

static void pomo_on_gesture(trev_app_t *a, trev_gesture_t g)
{
    // D18: the card owns the swipes while it is open, so an up-swipe scrolls the text instead of
    // leaving the ledger and a sideways swipe does not page the task out from under the card the
    // user is reading. 140px is about two thirds of the card's 220px height, so a swipe advances
    // most of a screenful and still leaves a couple of lines of overlap to read against.
    // Deliberately NOT swallowed here: FACE_DOWN / FACE_UP. Those are physical, they mean the
    // disk was turned over, and they route through set_view, which drops p_detail with the rest
    // of the face - so the card goes away as part of the state change rather than blocking it.
    if (p_detail) {
        switch (g) {
        case TREV_GESTURE_TAP_CONTENT: detail_close(); return;
        case TREV_GESTURE_SWIPE_UP:
        case TREV_GESTURE_SWIPE_DOWN:  return;   // the puck's card does not scroll
        case TREV_GESTURE_SWIPE_LEFT:
        case TREV_GESTURE_SWIPE_RIGHT: return;  // ignored on purpose: no sideways meaning in a card
        default: break;                         // face down / up: fall through to the handler below
        }
    }
    switch (g) {
    // Reuse the turn verbs rather than re-deriving them: a swipe means exactly what the
    // right/left tap zone means, so a divergence here would be a bug, not a feature.
    // The ledger is the exception: it has no task cursor to move, so a sideways swipe there
    // is ignored on purpose and SWIPE_UP (or the BACK pill) is the only way out.
    case TREV_GESTURE_SWIPE_LEFT:  if (s_view != PV_LEDGER) pomo_on_wheel(a, TREV_TURN_NEXT); break;
    case TREV_GESTURE_SWIPE_RIGHT: if (s_view != PV_LEDGER) pomo_on_wheel(a, TREV_TURN_PREV); break;
    case TREV_GESTURE_SWIPE_DOWN:
        if (s_view == PV_FOCUS || s_view == PV_PICKER) set_view(PV_LEDGER);   // pull today's record down
        break;
    case TREV_GESTURE_SWIPE_UP:
        if (s_view == PV_LEDGER) set_view(PV_FOCUS);
        break;
    // D18: a tap on the content area opens the full text of whichever task that face is about.
    // The ledger and the break face are ignored: neither is about one task, so there is nothing
    // for the card to be about either.
    case TREV_GESTURE_TAP_CONTENT:
        if (s_view == PV_FOCUS) detail_open(pomo_core_active_task(&s_pc));
        else if (s_view == PV_PICKER && s_pc.cursor >= 0 && s_pc.cursor < s_pc.tasks.count)
            detail_open(&s_pc.tasks.task[s_pc.cursor]);
        break;
    case TREV_GESTURE_FACE_DOWN:
        // Only from a running block. Face-down on an idle disk is how it sits on a desk, and
        // dropping that into a break face would mean the thing changes state while unwatched.
        if (s_pc.running) {
            pomo_core_toggle(&s_pc);
            s_face_break = true;
            s_break_left = s_break_secs;   // a fresh break, same as the block-complete path
            set_view(PV_BREAK);
            pomo_persist_save();
        }
        break;
    case TREV_GESTURE_FACE_UP:
        if (s_face_break) {
            s_face_break = false;
            pomo_core_toggle(&s_pc);   // resume exactly what face-down paused, BEFORE the face is
            set_view(PV_FOCUS);        // rebuilt, so it paints running instead of paused for a tick
            pomo_persist_save();
        }
        break;
    }
}

// D3: end-of-pomo flash. A brief full-screen coral pulse the instant a focus block completes,
// fired just before the face transitions to break (no speaker on this board, so the cue is
// purely visual). It fades from solid to transparent over one short cycle and self-deletes on
// completion. The overlay is parented to the SCREEN (s_pomo_root's parent), not s_pomo_root,
// on purpose: set_view(PV_BREAK) runs immediately after and lv_obj_clean(s_pomo_root) would
// otherwise wipe the overlay before a frame paints. Screen-parented + moved to foreground, it
// composes ON TOP of the break entrance for ~360ms, then removes itself. Safety: (1) the
// anim's var is the overlay, so a teardown of the whole app (BOOT-long home -> clear_screen ->
// lv_obj_clean(s_screen) deletes the overlay) auto-deletes the anim — no callback on freed
// mem; (2) the completed-cb deletes the overlay, so it never lingers.
static void flash_overlay_done(lv_anim_t *a)
{
    lv_obj_t *ov = (lv_obj_t *)a->var;
    if (ov) lv_obj_delete(ov);
}
static void flash_opa_exec(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void pomo_flash_complete(void)
{
    if (!s_pomo_root) return;
    lv_obj_t *scr = lv_obj_get_parent(s_pomo_root);   // the OS screen; survives s_pomo_root rebuild
    if (!scr) return;
    lv_obj_t *ov = lv_obj_create(scr);
    lv_obj_remove_style_all(ov);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ov, TT_CORAL, 0);    // one-accent-per-face: coral = the pomo done flash
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);  // starts solid, fades out
    lv_obj_move_foreground(ov);
    lv_anim_t a;                                   // fade solid -> transparent, ease-out, ~360ms
    lv_anim_init(&a);
    lv_anim_set_var(&a, ov);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 360);
    lv_anim_set_exec_cb(&a, flash_opa_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, flash_overlay_done);   // self-remove (no dangling overlay/anim)
    lv_anim_start(&a);
}



// Always-on background tick (A1): the single place the core countdown advances, running
// regardless of which app is mounted, so a focus block keeps counting on the launcher.
// When the focus face is mounted we repaint on a dirty tick and drive the focus->break
// transition; when it isn't, a completed block just stops (running=false) — we never
// switch faces from the background.
static void pomo_bg_tick_cb(lv_timer_t *t)
{
    (void)t;
    bool focus_mounted = (s_pomo_root != NULL && s_view == PV_FOCUS && !s_setmode);
    bool was_running = s_pc.running;
    uint32_t prev_left = s_pc.left_s;
    pomo_core_tick(&s_pc, lv_tick_get());
    bool crossed_min = (s_pc.left_s / 60 != prev_left / 60);   // minute boundary -> persist (A2)

    if (was_running && !s_pc.running && s_pc.left_s == 0) {       // block just completed
        pomo_persist_save();
        // B2/A6: record the completion in the durable outbox. Three facts, no prose: the
        // comment body is formatted at FLUSH time from these, so an offline day's pomos are
        // stored once and rendered once, and the wording can change without a migration.
        // Fires exactly once here (the was-running -> left_s==0 edge runs a single tick).
        // Guard on a non-empty id - serial-pushed tasks have none and must be skipped (the
        // helper no-ops anyway). Deliberately UNGATED: the outbox is also the local ledger the
        // C11 face and the C12 day arithmetic read, so a pomo is always recorded. Both write
        // flags (the TT_TODOIST_WRITE compile gate and the Settings kill-switch) gate the POST
        // inside the sync module, which is where they belong.
        const pomo_task_t *active = pomo_core_active_task(&s_pc);
        if (active && active->id[0])
            pomodoist_sync_log_pomo(active->id, (int64_t)time(NULL), (uint16_t)(s_pc.total_s / 60));
        if (focus_mounted) { s_break_left = s_break_secs; pomo_flash_complete(); set_view(PV_BREAK); return; }  // D3 flash + enter break (on-face only)
        // off-face: let it stop silently; the focus face will reflect it whenever it remounts
    } else if (crossed_min && s_pc.running) {
        pomo_persist_save();
    }
    if (focus_mounted && s_pc.dirty) { pomo_render(); s_pc.dirty = false; }
}

const trev_app_def_t POMODOIST_APP = {
    .api_version = TREV_APP_API_VERSION,
    .id = "pomodist",
    .name = "Pomodoist",
    .on_start = pomo_on_start,
    .on_stop = pomo_on_stop,
    .on_tick = pomo_on_tick,
    .on_turn = pomo_on_turn,
    .on_commit = pomo_on_commit,
    .on_gesture = pomo_on_gesture,   // v2: swipes + face down/up (the disk's only orientation input)
    .on_wheel = pomo_on_wheel,       // the puck: LENGTH on the focus face, PREV/NEXT on the picker
};


void pomodoist_ui_init(void)
{
    pomo_core_init(&s_pc);
    // Here, not in the sync module, because the sync module starts AFTER the LVGL timers.
    // A restored mid-block pomo can complete on the first tick and push onto a zeroed ring,
    // writing a one-item ring over the persisted one and erasing a whole offline day.
    outbox_init();   // idempotent: the sync module's later call is harmless
#if TT_DEV_DEMO   // force the demo task (has #tags + desc) for design-matching iteration
    seed_demo();
#else
    EXT_RAM_BSS_ATTR static pomo_tasklist_t cached;   // static: 9.7 KB at POMO_DESC_LEN 120, 21.2 KB at 480 on the disk; on the main stack it overflows
    // D3: NO seed_demo() fallback here. It used to fire whenever the cache was empty OR the
    // fetch failed, so a device with no tasks presented a plausible, wrong, unchanging list
    // ("Wire Todoist OAuth", "#deep-work") with nothing marking it as fake — and every sim
    // screenshot inherited the lie. An empty list is now genuinely empty, and the focus face
    // renders its real empty string, which until now was unreachable dead code.
    if (pomodoist_sync_cache_load(&cached) && cached.count > 0) {
        pomo_core_set_tasks(&s_pc, &cached);
    }
#endif
    pomo_persist_restore();   // A2: overlay saved timer (length/remaining/running) + clamp active onto the seeded list

    // D1: load device settings + apply BEFORE trev_open: break length + write-flag. The restored
    // timer, length included, is never touched here (#93).
    cfg_load();
    cfg_apply();

}
void pomodoist_ui_start(void) { lv_timer_create(pomo_bg_tick_cb, 1000, NULL); }
bool pomodoist_countdown_critical(void) { return s_pc.running && s_pc.left_s < 60; }
