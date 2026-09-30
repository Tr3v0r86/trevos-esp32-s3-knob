// pomodoist_sync.h — todoist-sync public API. Two transports, one tasklist contract.
//
// A (host push over USB-serial): a host script fetches today's Todoist tasks and writes one
// newline-terminated JSON line (see docs/contract.md) to the board's USB-Serial/JTAG.
//
// B (on-device wifi): the board fetches for itself, caches to NVS, and writes completions
// back. Built for a device that is online at a desk in the morning and offline all day
// afterwards, so it is a permanent loop, not a boot-time errand: wait for the link trev_net
// provides, flush the durable outbox, fetch, then hold the connection and refetch hourly.
// Nothing here ever blocks the app; the app reads the NVS cache. This component never brings
// Wi-Fi up (E7): trev_net owns the radio, the network walk and SNTP.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "pomodoist_core.h"   // pomo_tasklist_t

void pomodoist_sync_serial_start(void);          // transport A: install the USB RX task
bool pomodoist_sync_take(pomo_tasklist_t *out);  // true + copies out if a fresh list arrived

// transport B (see pomodoist_sync_wifi.c):
bool pomodoist_sync_cache_load(pomo_tasklist_t *out);   // true if a cached list exists (offline)

// Start the fetch loop. Call BEFORE trev_net_start (C1) so the GOT_IP handler exists when the
// link first comes up. The token is copied.
void pomodoist_sync_wifi_start(const char *token);

bool pomodoist_sync_connected(void);          // STA has an IP right now
bool pomodoist_sync_last_sync(time_t *out);   // wall time of the last successful fetch; false if never

// True once trev_net calls the clock TRUSTED THIS boot (SNTP landed, or a trusted source
// vouched). Distinct from "the clock looks believable": a board with a hardware RTC restores a
// believable clock at boot from its own battery-backed registers, so believable proves nothing
// about where the value came from. A board that writes its RTC back needs this one, or it
// writes the RTC from the RTC.
bool pomodoist_sync_clock_synced(void);

// write-back: device -> Todoist, one comment per completed pomodoro.
//
// The completion is persisted to the NVS outbox (pomodoist_outbox.h) before this returns, so
// a day's worth of offline pomos survives reboots and flushes in order on the next connect.
// The POST body is formatted at flush time from these three facts, never here. No-op if
// task_id is NULL/empty (transport A tasks have no id to write back to).
void pomodoist_sync_log_pomo(const char *task_id, int64_t ended_utc, uint16_t minutes);

// Runtime write kill-switch (the Settings toggle), layered on the TT_TODOIST_WRITE compile
// gate. Gates the POST only: completions still land in the outbox, which doubles as the local
// ledger of what was finished today. Default follows the compile gate.
void pomodoist_sync_set_write(bool enabled);
