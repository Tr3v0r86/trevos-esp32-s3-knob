// trev_net.h - the one Wi-Fi owner (T1, E7).
//
// C1 BOOT CONTRACT. main.c creates the netif layer, the default event loop and the SNTP
// service (esp_netif_sntp_init, autostart off) BEFORE trev_net_start(). trev_net never calls
// esp_netif_init, esp_event_loop_create_default or esp_netif_sntp_init: a second owner of any
// of the three aborts at boot. What trev_net does own: the STA netif (created exactly once,
// here and nowhere else), esp_wifi, the multi-SSID walk, and esp_netif_sntp_start() on GOT_IP.
//
// Time trust (E5, A4): NONE -> RESTORED (a clock nobody has vouched for this boot: kept across a
// reset, or read back from NVS; BSP_CLOCK_NVS boards only) -> TRUSTED (SNTP landed, or a caller
// vouched via trev_net_trust_time).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "trev_net_policy.h"

typedef struct { const char *ssid; const char *pass; } trev_net_ap_t;

// Once per boot; the list is copied, in preference order, first four kept. A second call logs
// a warning and returns. Never blocks: the walk runs in its own task.
void trev_net_start(const trev_net_ap_t *aps, int n);

bool trev_net_connected(void);                 // STA has an IP right now; safe from any task
trev_time_state_t trev_net_time_state(void);

// A trusted source (a Google response Date header) says the clock is right: settimeofday(utc)
// and move to TRUSTED.
void trev_net_trust_time(time_t utc);

// BSP_CLOCK_NVS boards, once, before the display: mark the clock RESTORED. A live clock that is
// already set (kept across a reset) is marked as it stands; only when it is unset is the saved NVS
// clock read back. A no-op on every other board.
void trev_net_clock_restore(void);

// Current link for the Settings face. Empty ssid and ip when down; rssi 0 when down. Any
// output may be NULL.
void trev_net_status(char *ssid, size_t ssid_cap, int *rssi, char *ip, size_t ip_cap);
