// trevos_ui.h — TrevOS shared UI kit. The polish vocabulary every Pomodist face is
// assembled from: status bar, hint bar, labels, tag chips, dots, description box,
// and the buttery arc tween. Built once on the focus face, reused on launcher /
// picker / break so each new face is assembly, not invention.
#pragma once
#include "lvgl.h"
#include "trevos_theme.h"

// TT_NO_LAYER_FX=1: this board cannot afford a composite layer, so every effect that makes
// LVGL allocate one is skipped (issue #36). Set build-wide by the board's top-level
// CMakeLists, because the guarded code spans both the `trevos` component and board `main`.
// Rationale and the exact failure mode are documented at the tween block in trevos_ui.c.
#ifndef TT_NO_LAYER_FX
#define TT_NO_LAYER_FX 0
#endif

// Light (paper) faces pass dark=false; the break face passes dark=true (carbon).
// btn_bg/btn_fg: the PRIMARY pill's fill + text (tt_pill kind 0). Added for pills (design
// task); skin->accent is wrong here for pomodoist (it would fill slate, not ink), so the
// primary pill gets its own pair of fields instead of reusing an existing one.
// btn_line/btn_fg2: the SECONDARY pill's outline + text (tt_pill kind 1). Added when Padlano
// (design task 3) needed its secondary pills outlined+texted in PAD_INK, which is neither
// skin->rule (too pale - it's a hairline colour, not a button-ink colour) nor skin->muted.
// Pomodoist's own secondary look (skin->rule border, skin->muted text) is unchanged - its
// two skins just carry those same values forward in the new fields.
typedef struct {
    lv_color_t ground, ink, muted, rule, accent;
    lv_color_t btn_bg, btn_fg;
    lv_color_t btn_line, btn_fg2;
} tt_skin_t;

// Skins (lazily built — lv_color_hex is not a constant expression, so not extern const).
const tt_skin_t *tt_skin_paper(void);   // focus / launcher / picker
const tt_skin_t *tt_skin_carbon(void);  // break

// Fill `root` with a skin's ground. Returns root for chaining.
lv_obj_t *tt_face_ground(lv_obj_t *root, const tt_skin_t *skin);

// Top status bar: app label (left) + live clock (right). *clock_out (if non-NULL)
// receives the clock label so the caller can update HH:MM. Aligns itself to the top.
lv_obj_t *tt_statusbar(lv_obj_t *root, const char *app, const tt_skin_t *skin, lv_obj_t **clock_out);

// Bottom hint bar, centred: "KEY > key_verb   |   BOOT • boot_verb". Button boards only.
lv_obj_t *tt_hintbar(lv_obj_t *root, const char *key_verb, const char *boot_verb, const tt_skin_t *skin);

// Bottom action bar for TOUCH boards (D1): three verbs laid over their own tap zones —
// left third / centre / right third — instead of naming buttons the board does not have.
// NULL or "" leaves a zone unlabelled. Children are left, mid, right in creation order.
lv_obj_t *tt_actionbar(lv_obj_t *root, const char *left, const char *mid, const char *right,
                       const tt_skin_t *skin);

// Relabel a round actionbar pill after the fact. zone: -1 left, 0 mid, +1 right. No-op if
// that zone had no pill drawn (a NULL/"" verb at tt_actionbar creation time). Rectangular
// glass callers already have their own label handles (children 0..2) and do not need this.
void tt_actionbar_set_verb(lv_obj_t *bar, int zone, const char *txt);

// Two-verb bar (or one centred verb when left is NULL): 96x44 pills on the bottom chord, the
// approved puck look (trev_bar2.h holds the pure numbers). Selects tt_actionbar_mode 2 (1 for a
// lone verb). The left pill is an ink outline, the right is filled. zones: -1 left, 0 right.
lv_obj_t *tt_actionbar2(lv_obj_t *root, const char *left, const char *right, const tt_skin_t *skin);

// Which verb layout tt_zone_at and tt_actionbar_top hit-test: 3 thirds (the default and what
// tt_actionbar sets), 2 halves (left half -1, right half 0), 1 one centred verb (always 0). In
// modes 1 and 2 the bar zone starts at tt_bar2_top. The touch classifiers already read both
// functions, so selecting a mode is the whole hook; boards that never call this stay on thirds.
void tt_actionbar_mode(int verbs);

// Wheel readout: a 300 degree turn glyph + a label in `col`, one row centred at y=cy. The label
// is returned through label_out.
lv_obj_t *tt_readout(lv_obj_t *root, int cy, lv_color_t col, lv_obj_t **label_out);

// ---- action bar geometry, shared by the drawer and the input glue ----
// tt_actionbar draws with exactly these numbers, and whatever hit-tests a press MUST use them
// too. When the bar was narrowed to clear round glass and the hit test still split the display
// into thirds, the labels and their zones drifted 23px apart: a press on a visible label fired
// the neighbouring verb. One source of truth is the fix.
//
// x0/w are display coordinates. On a rectangular board w is the full display width, so this is
// the old behaviour exactly.
void tt_actionbar_geom(int *x0, int *w);

// The bar's TOP edge in display y. Everything above it is content, everything from it down is
// chrome, and the input glue needs that line to tell a content tap from a verb tap. Same
// reason tt_actionbar_geom exists: the number is derived from what tt_actionbar actually drew,
// not copied into a board, so the two cannot drift.
int tt_actionbar_top(void);

// Which verb a press at display x belongs to: -1 left/prev, 0 centre/commit, +1 right/next.
// Boundaries are the BAR's internal thirds, but applied over the full display height so the
// targets stay finger-sized. No dead space: left of the bar is prev, right of it is next.
int tt_zone_at(int x);

// Label primitives (all return the label).
lv_obj_t *tt_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col, int track);
lv_obj_t *tt_eyebrow(lv_obj_t *parent, const char *txt, lv_color_t col);  // mono label, wide track
lv_obj_t *tt_title(lv_obj_t *parent, const char *txt, lv_color_t col);    // Archivo XB 20, wrapping
lv_obj_t *tt_numerals(lv_obj_t *parent, lv_color_t col);                  // Archivo XB 44 (set text later)

// Tag pill: filled rounded chip. bg = fill, fg = text. (Focus: TT_TAG_BG + TT_SLATE;
// picker: translucent paper on slate.) Returns the chip.
lv_obj_t *tt_chip(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg);

// Small dot — pagination / status bullet. filled=fill, else outline. d = diameter px.
lv_obj_t *tt_dot(lv_obj_t *parent, lv_color_t col, bool filled, int d);

// Drawn pill button. kind: 0 = primary (filled bg, fg text), 1 = secondary (2px border
// in `border`, transparent fill, fg text). small selects the compact metrics.
lv_obj_t *tt_pill(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg,
                  lv_color_t border, bool small, int kind);

// Description block: left rule (skin->rule) + wrapped body text in `txt_col`, fixed width w.
lv_obj_t *tt_descbox(lv_obj_t *parent, const char *txt, int w, const tt_skin_t *skin, lv_color_t txt_col);

// Buttery arc tween: animate an arc's value to `to` over `ms` with ease-out.
void tt_arc_anim(lv_obj_t *arc, int32_t to, uint32_t ms);

// ---- per-detent motion (DESIGN.md decisions log 2026-09-30, "Motion locked to the wheel") ----
#define TT_STEP_MS 150
typedef void (*tt_step_exec_t)(void *var, int32_t v);
// The live value, at the current tick, of the tween running on (var, exec), or `rest` when none runs.
// Matches ANY lv_anim on the pair (tt_arc_step relies on picking up tt_arc_anim's 600 ms tween).
int32_t tt_step_live(void *var, tt_step_exec_t exec, int32_t rest);
// Tween (var, exec) from `from` to `to` over `ms`, ease-out, replacing any tween running on the same
// pair (a retarget passes tt_step_live as `from`). settle(var), if given, runs once when the value
// lands. It is skipped when the var object is deleted, or when any other lv_anim_start or
// lv_anim_delete on the pair replaces the tween, not only when tt_step does. Without a wheel, or with ms 0, it lands at once: exec(var, to),
// then settle. Layer-free by contract: exec moves, re-angles or recolours, nothing else.
void tt_step(void *var, tt_step_exec_t exec, int32_t from, int32_t to, uint32_t ms, void (*settle)(void *var));
// A wheel scrub on an arc: tt_step on tt_arc_anim's own exec, from the live value, TT_STEP_MS, so it
// replaces a running 600 ms tween instead of racing it.
void tt_arc_step(lv_obj_t *arc, int32_t to);

// ---- generic micro-interaction tweens (Phase C) -------------------------------------
// All bind the lv_anim's `var` to `obj`, so LVGL auto-deletes the running anim when the
// object is freed (lv_obj_clean / set_view rebuild). No callback ever fires on a freed
// widget. They animate only cheap transform-like props (transform_scale, opa, translate)
// and always SETTLE to the object's natural resting value — the post-anim look is the
// same as if the helper had never run. Keep these to entrances/transitions, not loops.
//
// Emil Kowalski anchor: ease-out for entrances (fast then settle); springs/overshoot for
// playful pops; SHORT (120-250ms micro, 200-320ms transitions); subtle, purposeful.

// Scale-in pop: from `from_pct` (e.g. 92) up to 100% over `ms`, ease-out, centre pivot.
// Used for a tile select-pop / picker-card land. Settles at LV_SCALE_NONE (no transform).
void tt_anim_scale_in(lv_obj_t *obj, int from_pct, uint32_t ms);

// Playful overshoot pop: scales past 100% then settles (path_overshoot). For the launcher
// select-pop where a touch of bounce reads as "snap into place". Settles at no-transform.
void tt_anim_pop(lv_obj_t *obj, int from_pct, uint32_t ms);

// Fade-in: opa 0 -> cover over `ms`, ease-out, with `delay` ms before it starts (start
// value applied immediately so there's no one-frame flash of the settled opacity).
void tt_anim_fade_in(lv_obj_t *obj, uint32_t ms, uint32_t delay);

// Translate-in: slide from (dx,dy) offset to rest (0,0) over `ms`, ease-out. Pass one
// axis as 0. Used for the deselected->settle lift and small entrance slides.
void tt_anim_translate(lv_obj_t *obj, int dx, int dy, uint32_t ms);

// Cross-fade a PERSISTENT object (e.g. a face root reused across rebuilds): deletes any
// in-flight opa tween on it first, then fades opa 0 -> cover, ease-out. Safe to call on
// every rebuild without stacking fades. Settles fully opaque.
void tt_anim_root_fade(lv_obj_t *obj, uint32_t ms);

// Continuous breathe: gentle infinite scale pulse (100% -> `peak_pct` -> 100%) with a
// matched opacity dip, ease-in-out, `period` ms per half-cycle. For the break face amber
// dot. The anim's var is `obj`, so it is auto-removed when the dot is cleaned by set_view.
void tt_anim_breathe(lv_obj_t *obj, int peak_pct, uint32_t period);

// The standard entrance (design task): root cross-fades in over 160ms; hero (the face's
// biggest/most important element - countdown, headline) scale-pops in from 94% over 200ms;
// each row in `rows` fades in, staggered 40ms apart. rows may contain NULLs (a row a face
// didn't build this time) - those are skipped. Round faces call this at the end of every
// build_* function so every face enters the same way.
void tt_face_enter(lv_obj_t *root, lv_obj_t *hero, lv_obj_t **rows, int nrows);
