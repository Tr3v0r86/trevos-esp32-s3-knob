/* sim shim: esp_netif_sntp.h — host uses native time(), so SNTP is a no-op.
 *
 * `start` mirrors the real config field: on device it defaults true (the service starts at
 * init), and pomodoist_sync sets it false so it can arm SNTP on GOT_IP instead. The host has
 * a clock already, so sync_wait reports success immediately and the shell takes the same
 * branch it takes on a device whose clock did set.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>
typedef struct { const char *server; bool start; void (*sync_cb)(struct timeval *tv); } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(srv) { .server = (srv), .start = true, .sync_cb = NULL }
static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg) { (void)cfg; return ESP_OK; }
static inline esp_err_t esp_netif_sntp_start(void) { return ESP_OK; }
static inline esp_err_t esp_netif_sntp_sync_wait(int ticks) { (void)ticks; return ESP_OK; }
