// cal_model.h - the calendar window the Apps Script proxy returns, as fixed-size C.
//
// Plain C99, no IDF headers, so the parser runs and is tested on the host (make test).
// Contract: docs/superpowers/specs/2026-09-25-papercolor-calendar-design.md section 2.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Caps are #ifndef so a board without PSRAM can shrink the model (ADR-0017: the CYD builds
// 2 days, no descriptions). The defaults are the disk's and the page's, unchanged.
#ifndef CAL_MAX_DAYS
#define CAL_MAX_DAYS    17  // back 3 + days 14, the default window
#endif
#ifndef CAL_MAX_EVENTS
#define CAL_MAX_EVENTS  24  // the proxy caps at 24 too
#endif
#define CAL_TITLE_LEN   61  // 60 bytes + NUL (the proxy cuts at 60 chars; multibyte gets cut again here)
#define CAL_CALNAME_LEN 25
#ifndef CAL_DESC_LEN
#define CAL_DESC_LEN    241 // 240 bytes + NUL (the proxy cuts at 240 code points; multibyte gets cut again here)
#endif
#define CAL_LOC_LEN     61  // 60 bytes + NUL, same rule

// Spectra 6 panel indices, same values as boards/papercolor/main/spectra6.h.
#define CAL_PAL_BLACK  0x0
#define CAL_PAL_WHITE  0x1
#define CAL_PAL_YELLOW 0x2
#define CAL_PAL_RED    0x3
#define CAL_PAL_BLUE   0x5
#define CAL_PAL_GREEN  0x6

typedef struct {
    bool    allday;
    uint8_t sh, sm, eh, em;   // timed only; eh may be 24 (event runs past midnight)
    char    title[CAL_TITLE_LEN];
    char    cal[CAL_CALNAME_LEN];
    char    desc[CAL_DESC_LEN];   // optional; empty when the proxy sent none (desc=1 not asked, or no text)
    char    loc[CAL_LOC_LEN];     // optional
    uint8_t rgb[3];           // from "#rrggbb"; black when absent or malformed
    uint8_t pal;              // cal_pal_nearest(rgb), a CAL_PAL_* index
    bool    hidden;           // "vis":"private": the proxy already blanked it to BUSY
} cal_event_t;                // 398 bytes

typedef struct {
    char        date[11];     // "YYYY-MM-DD"
    uint8_t     n;
    uint8_t     more;         // events dropped past CAL_MAX_EVENTS, so a full day never reads as free
    cal_event_t ev[CAL_MAX_EVENTS];
} cal_day_t;                  // 9564 bytes: one NVS blob per day, under the 10 KB budget

typedef struct {
    char      generated[26];  // "2026-09-25T04:00:12+07:00"
    char      tz[24];
    uint8_t   n_days;
    cal_day_t day[CAL_MAX_DAYS];
} cal_window_t;               // 162639 bytes: PSRAM only

// Parse the proxy's JSON into *out (zeroed first). Tolerant: missing optional fields default,
// events past CAL_MAX_EVENTS and days past CAL_MAX_DAYS are dropped, a malformed time makes
// the event all-day, a day without a valid date is skipped. False only if the document is not
// the contract (not an object, or no "days_list" array). A true result can still hold zero
// days; the caller decides whether that is worth storing.
bool cal_model_parse(const char *json, size_t len, cal_window_t *out);

const cal_day_t *cal_window_find(const cal_window_t *w, const char *yyyy_mm_dd);
int cal_window_index_of(const cal_window_t *w, const char *date);   // -1 if absent

void cal_format_time(uint8_t h, uint8_t m, char out[6]);             // "09:05"

uint8_t cal_pal_nearest(uint8_t r, uint8_t g, uint8_t b);

// Index of the timed event with the earliest start at or after now_min (minutes since local
// midnight), or -1 when none is left today. All-day events are skipped. The face puts the
// now-line just above this row and snaps back to it.
int cal_day_next_at(const cal_day_t *d, int now_min);

// The event in progress: first timed event with start <= now_min < end, else -1.
int cal_day_current_at(const cal_day_t *d, int now_min);

// Stable sort of a day's events: all-day first (proxy order kept), then timed by start.
// The parser does not call it (the page relies on proxy order); the face does.
void cal_day_sort(cal_day_t *d);

// Chime bookkeeping keyed by WHAT starts, not by where it sits in the array: a resync that
// inserts or drops an earlier event reindexes the day, and an index mask would then re-fire
// one event and silence another. key = (start minute << 16) ^ (FNV-1a of the title & 0xFFFF).
// The ring holds the last 32 keys; the caller clears it on a date change.
typedef struct { uint32_t key[32]; uint8_t n, head; } cal_chime_ring_t;

// Number of timed events whose start is within [now_min + 1, now_min + 5] and whose key is
// not yet in the ring; each is added. The caller chimes ONCE when the result is > 0, so two
// events starting the same minute make one sound. An event that starts before 00:05 is
// unreachable by design (there is no "5 minutes before" on this day).
int cal_day_chime_due(const cal_day_t *d, int now_min, cal_chime_ring_t *ring);

#ifdef __cplusplus
}
#endif
