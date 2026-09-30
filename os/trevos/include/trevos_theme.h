// trevos_theme.h — TrevOS design tokens (from trevos-tokens.json, Paper·Slate language).
// Slate is the on-screen accent; coral = one accent per face; red is the puck bezel only.
#pragma once
#include "lvgl.h"

// Geometry seam. A round board defines this build-wide; everything else is rectangular.
// It selects the TYPE SCALE below, not the typefaces: the semantic roles are identical on
// every board, so no face ever asks what shape it is running on.
#ifndef TT_ROUND_DISPLAY
#define TT_ROUND_DISPLAY 0
#endif

#define TT_PAPER      lv_color_hex(0xF2EFE6)
#define TT_PAPER_ALT  lv_color_hex(0xFBFAF4)
#define TT_INK        lv_color_hex(0x1C1B19)
#define TT_MUTED      lv_color_hex(0x8A8472)
#define TT_RULE       lv_color_hex(0xE0DBCE)
#define TT_SLATE      lv_color_hex(0x3F5266)
#define TT_CORAL      lv_color_hex(0xE94F35)
#define TT_TAUPE      lv_color_hex(0xA9A093)
#define TT_GREEN      lv_color_hex(0x5E7B4E)
#define TT_DESC       lv_color_hex(0x5A5953)   // focus description body text (CSS #5a5953)
#define TT_TAG_BG     lv_color_hex(0xE7EAEE)   // focus tag pill fill (CSS #E7EAEE, cool)
#define TT_PIPE       lv_color_hex(0xD8D3C6)   // hint-bar pipe separator (CSS #D8D3C6)
#define TT_TICK       lv_color_hex(0xC9C2B2)   // ring cardinal tick marks (CSS #C9C2B2)
#define TT_RING_TRACK lv_color_hex(0xE7E2D5)   // ring background track (CSS #E7E2D5)
// The calendar's now-line and toast tint. v0 PLACEHOLDER: the design pass names and tunes
// this. Not coral (Pomodoist's), not slate (the OS's).
#define TT_NOW        lv_color_hex(0x2F6F8F)

// break face (carbon · amber)
#define TT_CARBON     lv_color_hex(0x131210)
#define TT_CARBON_TX  lv_color_hex(0xEDE7DA)
#define TT_CARBON_MU  lv_color_hex(0x8A8378)
#define TT_AMBER      lv_color_hex(0xE0A24E)
#define TT_BREAK_HEAD lv_color_hex(0xF3E9D6)   // break "Break" headline (CSS #F3E9D6)
#define TT_BREAK_BODY lv_color_hex(0xD8C9B6)   // break body / labels (CSS #D8C9B6)
#define TT_DASH       lv_color_hex(0xC5BEAE)   // picker prev/next dashed card border (CSS #C5BEAE)
#define TT_CARD_SUB   lv_color_hex(0xC9D2DC)   // text on slate cards (CSS #C9D2DC)
#define TT_CARD_PILL  lv_color_hex(0x54657A)   // tag pill on a slate card (≈ translucent paper over slate)

// fonts — generated from the TTFs in boards/tdisplay-s3/tools/fonts/ with lv_font_conv,
// 4bpp, all --no-compress. Two scales, one per panel geometry; see the roles below.
//
// The sizes are NOT the .dc.html master ÷2 any more. That master renders a 320x170 glass at
// 2x, and its small text lands at 5-7px physically, which is unreadable on real glass; the
// rectangular scale was already bumped ~1.5x for that reason. The round scale is derived from
// the panel instead: a 360x360 circle is 4.0x the area of a 320x170 and is read at desk
// distance, so it gets its own sizes rather than a multiplier on someone else's.
// ---- typefaces: IBM Plex ------------------------------------------------------------------
// IBM Plex Sans + IBM Plex Mono, one superfamily so the sans and the mono share metrics and
// skeleton. They sit adjacent constantly (status bar: mono clock beside sans app name), and
// two unrelated families is what made that row read as two designs. SIL OFL, so the generated
// bitmaps here are redistributable; licence travels in tools/fonts/IBMPlex-OFL.txt.
//
// The countdown is MONO, deliberately. A proportional face re-flows the whole numeral block
// every time a digit changes width, so a ticking timer jitters horizontally at the exact
// moment you are staring at it. Mono makes tabular figures structural rather than a property
// you hope the designer kept. Both numeral sizes carry digits, colon and period ONLY - do not
// point TT_F_NUM at a label, it has no letters (see app_ledstrip.c).
LV_FONT_DECLARE(plex_mono_b_72)    // hero countdown, round
LV_FONT_DECLARE(plex_mono_b_25)    // hero countdown, rectangular
LV_FONT_DECLARE(plex_sans_b_28)    // break-face headline
LV_FONT_DECLARE(plex_sans_b_22)    // titles, round
LV_FONT_DECLARE(plex_sans_b_16)    // titles rectangular / tiles round
LV_FONT_DECLARE(plex_sans_b_12)    // tiles, rectangular
LV_FONT_DECLARE(plex_sans_r_14)    // body, round
LV_FONT_DECLARE(plex_sans_r_10)    // body, rectangular
LV_FONT_DECLARE(plex_mono_sb_13)   // status + labels, round
LV_FONT_DECLARE(plex_mono_sb_11)   // status rectangular / tiny round
LV_FONT_DECLARE(plex_mono_sb_10)   // labels, rectangular
LV_FONT_DECLARE(plex_mono_sb_9)    // tiny, rectangular

// ---- semantic type roles ------------------------------------------------------------------
// Faces reference ONLY these. Geometry picks the size; the role never changes meaning. This is
// what lets one design language survive three panel shapes: TT_F_NUM always means "the hero
// number", and it is 25px on a T3 and 72px on the round with no face aware of the difference.
#if TT_ROUND_DISPLAY
#define TT_F_NUM      (&plex_mono_b_72)
#define TT_F_TITLE    (&plex_sans_b_22)
#define TT_F_BODY     (&plex_sans_r_14)
#define TT_F_STATUS   (&plex_mono_sb_13)
#define TT_F_LABEL    (&plex_mono_sb_13)
#define TT_F_LABEL_SM (&plex_mono_sb_11)   // small pill text, round
#define TT_F_TILE     (&plex_sans_b_16)
#define TT_F_HEAD     (&plex_sans_b_28)
#define TT_F_TINY     (&plex_mono_sb_11)
#else
#define TT_F_NUM      (&plex_mono_b_25)
#define TT_F_TITLE    (&plex_sans_b_16)
#define TT_F_BODY     (&plex_sans_r_10)
#define TT_F_STATUS   (&plex_mono_sb_11)
#define TT_F_LABEL    (&plex_mono_sb_10)
#define TT_F_LABEL_SM (&plex_mono_sb_9)    // small pill text, rectangular
#define TT_F_TILE     (&plex_sans_b_12)
#define TT_F_HEAD     (&plex_sans_b_28)
#define TT_F_TINY     (&plex_mono_sb_9)
#endif

// spacing / forms — device px (design tokens ÷2). Mono tracking ~0.2em @5px ≈ 1px.
#define TT_PAD        11   // status/hint bar h-padding (CSS 22px)
#define TT_GAP         8
#define TT_R_CARD      7   // card radius (CSS 14px)
#define TT_R_CHIP      6
#define TT_R_PILL    999
#define TT_TRACK       1   // mono label tracking (tight)
#define TT_TRACK_WIDE  1   // eyebrow / FOCUS tracking (~0.2em at 5px ≈ 1px)
#if TT_ROUND_DISPLAY
#define TT_BAR_STATUS 30   // room for 13px mono
#define TT_BAR_HINT   30
// The rim ring owns the outermost band of the glass, so every chord-clamped bar has to be
// measured against the radius INSIDE it, not against the glass. Without this the bars are
// wider than the ring at their own height and their ends cross straight through it.
#define TT_ROUND_RING 14   // ring stroke 12 + 2px breathing room
#else
#define TT_BAR_STATUS 24   // status bar height (room for 11px mono)
#define TT_BAR_HINT   24   // hint bar height (room for 10px mono)
#endif

// glyphs carried by every plex_mono_sb_* size (UTF-8). The mono range includes 0x2022 and
// 0x203A explicitly; the sans range does not, so these belong on mono labels only.
#define TT_GLYPH_CHEVRON  "\xE2\x80\xBA"  // > (U+203A)
#define TT_GLYPH_BULLET   "\xE2\x80\xA2"  // • (U+2022)
#define TT_GLYPH_MIDDOT   "\xC2\xB7"      // · (U+00B7)

// E-paper override (ADR-0014, 2026-09-06). Spectra 6 has six colours and none of them
// is a grey, so every secondary tone in the block above quantises to yellow or green: TT_TAUPE
// becomes faint yellow on a white ground and disappears. Greys stay on green until roughly
// #303030, so there is no darker grey to retreat to. Secondary text therefore goes to ink here
// and the hierarchy is carried by weight, size and spacing, which is what print does.
//
// Accents are deliberately NOT overridden. The panel genuinely has red, yellow, blue and green,
// and TT_CORAL already lands on red cleanly. sim/shims/test_theme_spectra6.c pins all of it.
#ifndef TT_EPAPER
#define TT_EPAPER 0
#endif
#if TT_EPAPER
#undef  TT_TAUPE
#undef  TT_MUTED
#undef  TT_DESC
#define TT_TAUPE      TT_INK
#define TT_MUTED      TT_INK
#define TT_DESC       TT_INK
#endif
