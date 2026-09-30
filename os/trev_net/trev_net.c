// trev_net.c - the one Wi-Fi owner. Lifted from apps/pomodoist/sync/pomodoist_sync_wifi.c
// (wifi_stack_init, connect_any, sntp_sync_once, the hourly walk-back), with the attempt loop
// replaced by trev_walk_fail. C1 contract: see trev_net.h. This file never makes the three
// init calls that header names; main.c owns them.
#include "trev_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "trev_net";

#define STARTED_BIT      BIT0   // esp_wifi_start has been acknowledged by the driver
#define GOT_IP_BIT       BIT1   // STA has an IP
#define DISCONNECTED_BIT BIT2   // STA dropped (or failed to associate)
#define NEVER_BIT        BIT3   // nothing sets it: lets an offline sleep tick like a wait

#define TREV_NET_MAX       4
#define CONNECT_WAIT_MS    15000        // per attempt
#define OFFLINE_RETRY_MS  (5 * 60000)   // the whole walk failed: sleep before walking again
#define WALKBACK_MS      (60 * 60000)   // hourly, leave a fallback net for the preferred one
#define SAVE_MS               60000     // clock save cadence while TRUSTED

typedef struct { char ssid[33], pass[65]; } net_t;

static net_t s_nets[TREV_NET_MAX];
static int   s_net_count;
static bool  s_started;
static EventGroupHandle_t s_eg;
static esp_netif_t *s_netif;
static volatile bool s_connected;
static volatile trev_time_state_t s_time = TREV_TIME_NONE;
static bool  s_sntp_ok;                 // SNTP itself has landed this boot; a trust_time vouch does not set it
static int   s_net_idx = -1;            // index in s_nets of the network we are on; -1 = none

// ---------------------------------------------------------------- clock persistence

#ifdef BSP_CLOCK_NVS
#define NVS_NS  "trev_net"
#define NVS_KEY "clock"

static void clock_save(void)
{
    time_t now = time(NULL);
    if (s_time != TREV_TIME_TRUSTED || now < TREV_EPOCH_2020) return;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_i64(h, NVS_KEY, (int64_t)now);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "clock save failed: %s", esp_err_to_name(e));
}

void trev_net_clock_restore(void)
{
    nvs_handle_t h;
    int64_t saved = 0;
    // The RTC domain keeps the system time across a chip reset and esp_restart. Nobody has vouched
    // for it this boot, so it is RESTORED ("~HH:MM", no chime) until SNTP or a Date header (A4, D8).
    if (time(NULL) >= TREV_EPOCH_2020) {
        s_time = trev_time_on_restore(s_time);
        ESP_LOGI(TAG, "clock kept across reset (RESTORED, not trusted)");
        return;
    }
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    esp_err_t e = nvs_get_i64(h, NVS_KEY, &saved);
    nvs_close(h);
    if (e != ESP_OK || !trev_clock_should_restore(time(NULL), (time_t)saved)) return;
    struct timeval tv = { .tv_sec = (time_t)saved };
    settimeofday(&tv, NULL);
    s_time = trev_time_on_restore(s_time);
    ESP_LOGI(TAG, "clock restored from NVS (RESTORED, not trusted)");
}
#else
static void clock_save(void) { }
void trev_net_clock_restore(void) { }
#endif

// ---------------------------------------------------------------- time

trev_time_state_t trev_net_time_state(void) { return s_time; }

void trev_net_trust_time(time_t utc)
{
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    s_time = trev_time_on_trusted(s_time);
}

// An SNTP request needs a route, so this runs on the first connect rather than at boot.
// main.c initialised the service with autostart off precisely so this call arms it. Then we
// block for the answer: lwIP defers its first request by a random 0-5000ms and will not retry
// for 15s, so anything that does not wait is gambling on a coin flip it usually loses.
static void sntp_sync_once(void)
{
    if (s_sntp_ok) return;
    esp_netif_sntp_start();   // ESP_ERR_INVALID_STATE on a later connect is expected and harmless
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(12000)) == ESP_OK) {
        s_sntp_ok = true;
        s_time = trev_time_on_trusted(s_time);
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        ESP_LOGI(TAG, "clock trusted (SNTP): %04d-%02d-%02d %02d:%02d local",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    } else {
        ESP_LOGW(TAG, "clock NOT synced in 12s");
    }
}

// ---------------------------------------------------------------- wifi

// Events only set bits. The loop owns every esp_wifi_connect() call, because it is the only
// thing that knows which network in the list it is currently trying; auto-reconnecting on
// DISCONNECTED would fight the multi-SSID walk.
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_eg, STARTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_eg, GOT_IP_BIT);
        xEventGroupSetBits(s_eg, DISCONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupClearBits(s_eg, DISCONNECTED_BIT);
        xEventGroupSetBits(s_eg, GOT_IP_BIT);
    }
}

static bool wifi_stack_init(void)
{
    s_eg = xEventGroupCreate();   // before esp_wifi_start: the handlers set bits in it
    if (!s_eg) return false;
    s_netif = esp_netif_create_default_wifi_sta();   // the one and only STA netif (C1)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_init failed"); return false; }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_start failed"); return false; }
    // esp_wifi_connect() is only legal once the driver has raised STA_START, and nothing
    // auto-connects for us, so wait for it here rather than racing the first attempt.
    return (xEventGroupWaitBits(s_eg, STARTED_BIT, pdFALSE, pdFALSE,
                                pdMS_TO_TICKS(3000)) & STARTED_BIT) != 0;
}

// Waits up to ms for any of `bits` (cleared on exit), saving the clock once a minute on the
// way. Returns the bits that fired, 0 on timeout.
static EventBits_t wait_saving(EventBits_t bits, uint32_t ms)
{
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    for (;;) {
        int32_t left = (int32_t)(end - xTaskGetTickCount());   // signed: survives tick wrap
        if (left <= 0) return 0;
        TickType_t slice = (TickType_t)left < pdMS_TO_TICKS(SAVE_MS) ? (TickType_t)left
                                                                     : pdMS_TO_TICKS(SAVE_MS);
        EventBits_t b = xEventGroupWaitBits(s_eg, bits, pdTRUE, pdFALSE, slice);
        if (b & bits) return b;
        clock_save();
    }
}

// Walk the list in preference order (home first, hotspot second) and stop at the first
// network that hands us an IP. trev_walk_fail owns the arithmetic: 3 tries per net, every net,
// 2 rounds. Returns false when the whole walk is spent.
static bool connect_any(void)
{
    trev_walk_t w = {0};
    for (;;) {
        wifi_config_t wc = {0};
        // Full width: wc is zeroed, and the IDF takes a 32-byte SSID / 64-byte PSK unterminated,
        // so a legal maximum-length credential is not cut short.
        strncpy((char *)wc.sta.ssid, s_nets[w.net].ssid, sizeof wc.sta.ssid);
        strncpy((char *)wc.sta.password, s_nets[w.net].pass, sizeof wc.sta.password);
        if (esp_wifi_set_config(WIFI_IF_STA, &wc) == ESP_OK) {
            xEventGroupClearBits(s_eg, GOT_IP_BIT | DISCONNECTED_BIT);
            ESP_LOGI(TAG, "sta try %s", s_nets[w.net].ssid);
            if (esp_wifi_connect() == ESP_OK) {
                EventBits_t b = xEventGroupWaitBits(s_eg, GOT_IP_BIT | DISCONNECTED_BIT, pdFALSE,
                                                    pdFALSE, pdMS_TO_TICKS(CONNECT_WAIT_MS));
                if (b & GOT_IP_BIT) {
                    s_net_idx = w.net;
                    ESP_LOGI(TAG, "connected: %s", s_nets[w.net].ssid);
                    return true;
                }
                esp_wifi_disconnect();          // release this SSID before retrying or moving on
                vTaskDelay(pdMS_TO_TICKS(200)); // let trailing events drain, so a late
                                                // DISCONNECTED does not fail the next attempt
            }
        }
        if (trev_walk_fail(&w, s_net_count) == TREV_WALK_SLEEP) break;
    }
    s_net_idx = -1;
    return false;
}

static void net_task(void *arg)
{
    (void)arg;
    if (!wifi_stack_init()) {
        ESP_LOGE(TAG, "wifi stack init failed; offline for this boot");
        vTaskDelete(NULL);
    }
    for (;;) {
        if (!connect_any()) {
            // Nothing reachable is the normal state all day at work; block, never poll.
            ESP_LOGW(TAG, "no known network; retry in %d min", OFFLINE_RETRY_MS / 60000);
            wait_saving(NEVER_BIT, OFFLINE_RETRY_MS);
            continue;
        }
        sntp_sync_once();

        // Stay associated until it drops, or the hour is up and we are on a fallback net.
        // Without the walk-back one 60-second home AP reboot parks the device on the hotspot
        // for the day; dropping the link sends us round the outer loop, back at net 0.
        for (;;) {
            if (wait_saving(DISCONNECTED_BIT, WALKBACK_MS)) { ESP_LOGW(TAG, "wifi dropped; reconnecting"); break; }
            if (s_net_idx > 0) {
                ESP_LOGI(TAG, "on fallback net %d; retrying the preferred network", s_net_idx + 1);
                esp_wifi_disconnect();
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- public API

void trev_net_start(const trev_net_ap_t *aps, int n)
{
    if (s_started) { ESP_LOGW(TAG, "trev_net_start called twice; ignored"); return; }
    if (!aps || n <= 0) { ESP_LOGE(TAG, "no networks configured; wifi not started"); return; }
    if (n > TREV_NET_MAX) {
        ESP_LOGW(TAG, "%d networks given, only the first %d are kept", n, TREV_NET_MAX);
        n = TREV_NET_MAX;
    }
    for (int i = 0; i < n; i++) {
        if (!aps[i].ssid || !aps[i].ssid[0]) continue;   // an empty slot is a placeholder
        strncpy(s_nets[s_net_count].ssid, aps[i].ssid, sizeof s_nets[0].ssid - 1);
        if (aps[i].pass) strncpy(s_nets[s_net_count].pass, aps[i].pass, sizeof s_nets[0].pass - 1);
        s_net_count++;
    }
    if (s_net_count == 0) { ESP_LOGE(TAG, "every network slot was empty; wifi not started"); return; }
    s_started = true;
    ESP_LOGI(TAG, "starting with %d network(s)", s_net_count);
    xTaskCreate(net_task, "trev_net", 4096, NULL, 5, NULL);
}

bool trev_net_connected(void) { return s_connected; }

void trev_net_status(char *ssid, size_t ssid_cap, int *rssi, char *ip, size_t ip_cap)
{
    if (ssid && ssid_cap) ssid[0] = 0;
    if (ip && ip_cap) ip[0] = 0;
    if (rssi) *rssi = 0;
    if (!s_connected) return;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        if (ssid && ssid_cap) snprintf(ssid, ssid_cap, "%s", (const char *)ap.ssid);
        if (rssi) *rssi = ap.rssi;
    }
    esp_netif_ip_info_t info;
    if (ip && ip_cap && s_netif && esp_netif_get_ip_info(s_netif, &info) == ESP_OK)
        snprintf(ip, ip_cap, IPSTR, IP2STR(&info.ip));
}
