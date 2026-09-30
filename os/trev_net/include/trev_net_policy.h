// trev_net_policy.h - clock trust, HTTP Date parsing and the Wi-Fi walker, pure (C99 only,
// no IDF) so a host test can run it. trev_net.c drives these from the Wi-Fi events.
#pragma once
#include <stdbool.h>
#include <time.h>

#define TREV_EPOCH_2020  1577836800   // any clock below this has never been set
#define TREV_WALK_TRIES  3            // attempts per network, per round
#define TREV_WALK_ROUNDS 2            // full passes over the list before sleeping

// NONE: never set. RESTORED: a clock nobody has vouched for this boot (kept across a reset, or read
// back from NVS), believable enough to show as "~HH:MM" but not to chime or publish. TRUSTED: SNTP or a Google response Date header vouched for it (E5, A4).
typedef enum { TREV_TIME_NONE, TREV_TIME_RESTORED, TREV_TIME_TRUSTED } trev_time_state_t;

// Restore the saved clock only when the live one is unset (< 2020) and the saved one is real.
// An esp_restart keeps the chip's time, and that must never be overwritten by an older save.
bool trev_clock_should_restore(time_t now, time_t saved);

// A restore never demotes a TRUSTED clock; trust never regresses.
trev_time_state_t trev_time_on_restore(trev_time_state_t s);
trev_time_state_t trev_time_on_trusted(trev_time_state_t s);

// RFC 7231 IMF-fixdate, e.g. "Tue, 29 Sep 2026 06:18:27 GMT". False on anything else.
bool trev_http_date_parse(const char *s, time_t *out);

// Position in the walk: which network, which attempt on it, which pass over the list.
typedef struct { int net, attempt, round; } trev_walk_t;
typedef enum { TREV_WALK_TRY, TREV_WALK_SLEEP } trev_walk_act_t;

// Call after each failed attempt (the first attempt is made from the zeroed struct). TRY: go
// again at w->net. SLEEP: TRIES x n x ROUNDS attempts are spent; w is reset to net 0.
trev_walk_act_t trev_walk_fail(trev_walk_t *w, int n);
