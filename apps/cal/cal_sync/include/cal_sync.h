// cal_sync.h - the calendar window on the device: fetch, cache, publish.
//
// Owns nothing about Wi-Fi. It registers its own IP_EVENT_STA_GOT_IP handler and fetches 60 s
// after every connect and every 5 minutes after, into a window in PSRAM, then into NVS. Faces
// poll cal_sync_generation() and copy the day they need; they never hold a pointer in here.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "cal_model.h"

// url: the Apps Script /exec URL. cal_id: the calendar id to filter on (may be NULL for all).
// Loads the NVS cache and bumps the generation before returning, so a boot face has data.
// Call once, after esp_event_loop_create_default and nvs init, and before Wi-Fi starts.
void     cal_sync_start(const char *url, const char *cal_id);
uint32_t cal_sync_generation(void);                            // 0 until the first publish
bool     cal_sync_get_day(const char *yyyy_mm_dd, cal_day_t *out);  // copies under lock; false if absent
bool     cal_sync_last_sync(time_t *out);                      // wall time of the last good fetch; false if never

// All arguments must remain valid for the app lifetime. Credentials provisioned over USB.
void cal_sync_start_google(const char *client, const char *secret, const char *refresh, const char *cal_id);

// Transport A (ADR-0017 MVP): no network; the laptop pushes Google pages over the console UART
// with tools/push-cal.py, which also sets the clock. Call instead of the two above.
void cal_sync_start_serial(void);

// Time trust and link state (E5, D10). Both hooks are optional; NULL keeps the old behaviour.
// trusted: false holds a fetched window back (nothing cached, nothing published) and refetches
// once in 5 s. http_date: gets the Date header of every Google response, for the clock owner.
// Set before the cal_sync_start_* call; the function pointers must outlive it.
void cal_sync_set_time_hooks(bool (*trusted)(void), void (*http_date)(const char *date));

// What the faces say when there is no cached day. NONE: no fetch transport configured.
// EXPIRED: the token endpoint refused the refresh token (invalid_grant). OK: otherwise.
typedef enum { CAL_LINK_NONE, CAL_LINK_OK, CAL_LINK_EXPIRED } cal_link_t;
cal_link_t cal_sync_link(void);

// Settings 2.0 rows. request_now: Calendar > Sync now; sets the fetch bit through the arm path,
// a no-op (one log line) before start or on the serial transport. clear_cache: Reset > Clear
// calendar cache; erases the cal namespace on cache and republishes an empty window.
void cal_sync_request_now(void);
void cal_sync_clear_cache(void);
