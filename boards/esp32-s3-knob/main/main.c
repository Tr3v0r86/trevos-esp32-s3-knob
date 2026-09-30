#include "pomodoist_ui.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "bsp_display.h"
#if BSP_INPUT_TOUCH
#include "bsp_touch.h"        // CYD: resistive touch, tap-zones -> trev verbs
#else
#include "bsp_buttons.h"      // T-Display-S3: two physical buttons -> trev verbs
#endif
// The disk's optional hardware. Each block is a board -D, never a board name, so a second
// round board with a different sensor set gets the parts it has and none of the parts it
// does not. The sim defines none of them, which is why it still links with no stubs.
#include "bsp_wheel.h"        // the puck: PCNT wheel sampled at 1 ms into a queue, drained below
#ifdef BSP_HAS_HAPTIC
#include "bsp_haptic.h"       // the puck: DRV2605 tick / thunk, probed (absent = every call a no-op)
#endif
#include "trevos.h"
#include "trevos_theme.h"
#include "trevos_ui.h"
#include "trev_bar2.h"        // the puck: tt_bar2_x/top, the lone SKIP pill sits where the classifier's zone is
#include "trevos_shot.h"
#include "pomodoist_core.h"
#include "pomodoist_sync.h"
#include "trev_net.h"      // the one Wi-Fi owner (T1, E7): started below, and the source of time trust (A4)
#include "trevos_home.h"
#include "trevos_settings.h"
#include "trev_prefs.h"
#include "esp_app_desc.h"     // About row: the running image's version
#include "esp_system.h"       // esp_restart (Restart, Reset preferences)
#define TT_SETTINGS_ROWS 1    // the puck and the disk compile and bind the Settings rows (E14); only the ring home registers the face
#include "cal_ui.h"
#include "cal_sync.h"
#include "esp_netif_sntp.h"
#include <sys/time.h>   // struct timeval, for the SNTP sync callback
#include <secrets.h>   // angle brackets: resolve via each board's -I order, not this file's directory (the disk has its own secrets.h)
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR (no-op unless SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_event.h"

esp_err_t bsp_backlight_set(uint8_t percent);


#ifndef BSP_DISP_SWAP_XY
#define BSP_DISP_SWAP_XY true
#endif
#ifndef BSP_DISP_MIRROR_X
#define BSP_DISP_MIRROR_X false
#endif
#ifndef BSP_DISP_MIRROR_Y
#define BSP_DISP_MIRROR_Y true
#endif
#ifndef BSP_DISP_SWAP_BYTES
#define BSP_DISP_SWAP_BYTES false   // S3 i80 path: no swap. SPI ILI9341 (CYD) needs true.
#endif

// The page overrides this to pc_wifi_wanted() (main/CMakeLists.txt): a mains-powered board
// has no reason to gate the radio, but a battery board on a timer wake most wakes has no
// reason to bring it up either.
#ifndef BSP_WIFI_THIS_WAKE
#define BSP_WIFI_THIS_WAKE() 1   // every mains-powered board: always
#endif

static const char *TAG = "knob";



#ifndef TT_DEV_DEMO
#define TT_DEV_DEMO 0   // 1 = force demo tasks (tags+desc) for design iteration; 0 = live Todoist
#endif
#ifndef TT_DEV_DEMO_LATE
#define TT_DEV_DEMO_LATE 0
#endif
#ifndef TT_DEV_NO_AMBIENT
#define TT_DEV_NO_AMBIENT 0
#endif
#ifndef TT_DEV_VIEW
#define TT_DEV_VIEW 0
#endif
// Master gate for device->Todoist writes (Phase B). Default ON; set 0 for safe bring-up
// with zero live POSTs. Threaded through to the sync module (which has its own matching
// gate) so write-back can be killed from one place here too.
#ifndef TT_TODOIST_WRITE
#define TT_TODOIST_WRITE 1
#endif
#ifndef TT_BOOT_APP
#define TT_BOOT_APP 2   // registration index the TT_CAL set boots into: 2 = Cal (the disk), 1 =
                        // Pomodoist (the puck, UC1 B / D11). Home (0) is never a boot face.
#endif
#ifndef TT_DEV_RING_FILL
#define TT_DEV_RING_FILL 0   // sim fixture (pucksim home8): register dummy apps until the ring holds this many
#endif
#ifndef TT_CAL_SERIAL
#define TT_CAL_SERIAL 0 // 1 = no Wi-Fi at all; the laptop pushes the calendar over the console UART
#endif
#include "pomodoist_outbox.h"   // today's completions, offline-safe (C12's DONE estimate)
#if TT_CAL && defined(BSP_HAS_HAPTIC)
// Y5: the calendar chime on a board with no speaker is a thunk plus a flash. The backlight goes to
// 100 and a full-screen ink object on the top layer is shown, hidden, shown, 150 ms each, then
// deleted: three visible beats a dark desk notices, with no opacity animation (a fade is a
// continuous full-frame redraw on a QSPI panel). Runs on the LVGL task (disk_tick_cb calls it).
// The backlight is put back by disk_tick_cb's own dim logic: it writes the duty only on a change,
// so the flash raises s_bl_restore and the next 1 s tick re-writes whatever level the idle
// clock wants (up to 1 s of extra brightness afterwards, and nothing invented here).
static lv_obj_t *s_flash_ov;
static uint8_t   s_flash_step;
static bool      s_bl_restore;

static void chime_flash_cb(lv_timer_t *t)
{
    (void)t;   // repeat_count 3: LVGL deletes the timer itself after the third call
    s_flash_step++;
    if      (s_flash_step == 1) lv_obj_add_flag(s_flash_ov, LV_OBJ_FLAG_HIDDEN);
    else if (s_flash_step == 2) lv_obj_clear_flag(s_flash_ov, LV_OBJ_FLAG_HIDDEN);
    else { lv_obj_delete(s_flash_ov); s_flash_ov = NULL; s_bl_restore = true; }
}

static void chime_flash(void)
{
    if (s_flash_ov) return;   // one flash at a time
    bsp_backlight_set(100);
    s_flash_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_flash_ov);
    lv_obj_clear_flag(s_flash_ov, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_flash_ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_flash_ov, TT_INK, 0);
    lv_obj_set_style_bg_opa(s_flash_ov, LV_OPA_COVER, 0);
    s_flash_step = 0;
    lv_timer_set_repeat_count(lv_timer_create(chime_flash_cb, 150, NULL), 3);
}
#endif
#if !BSP_INPUT_TOUCH
static void on_btn(bsp_btn_t btn, bsp_btn_evt_t evt, void *ctx)
{
    (void)ctx;
    trev_input_note_activity();
    if (!lvgl_port_lock(0)) return;
#if BSP_BTN3
    // Three buttons, so KEY is PREV here instead of the two-button NEXT below (BSP_BTN_C
    // below takes NEXT instead). Per board_pins.h/bsp_buttons.c's s_pins mapping this is
    // physical A; see the BSP_BTN_C arm just under this block for the full A/B/C mapping.
    if      (btn == BSP_BTN_KEY  && evt == BSP_BTN_PRESS) trev_input_turn(TREV_TURN_PREV);
#else
    if      (btn == BSP_BTN_KEY  && evt == BSP_BTN_PRESS) trev_input_turn(TREV_TURN_NEXT);
#endif
    else if (btn == BSP_BTN_KEY  && evt == BSP_BTN_LONG)  trev_input_turn(TREV_TURN_PREV);
    else if (btn == BSP_BTN_BOOT && evt == BSP_BTN_PRESS) trev_input_commit();
    else if (btn == BSP_BTN_BOOT && evt == BSP_BTN_LONG)  trev_input_home();
#if BSP_BTN3
    // The page's third button, physical C, is NEXT: nothing needs a long press to reach any
    // of the three verbs. Long-C stays HOME, which on a one-app board is a redraw of the same
    // face. Composed with bsp_buttons.c's s_pins mapping, physical A=PREV (BSP_BTN_KEY, just
    // above), B=COMMIT (BSP_BTN_BOOT, unchanged above), C=NEXT - the left-to-right pairing the
    // pin choice was aiming for. Settled at task 8 review (see bsp_buttons.c's s_pins
    // comment); not an open decision.
    else if (btn == BSP_BTN_C && evt == BSP_BTN_PRESS) trev_input_turn(TREV_TURN_NEXT);
    else if (btn == BSP_BTN_C && evt == BSP_BTN_LONG)  trev_input_home();
#endif
    lvgl_port_unlock();
}
#endif

// Settings: what the one dimmer (disk_tick_cb) reads. Defaults are the values it hard-coded
// before (100 percent, dim after 60 s), so a board that never loads prefs behaves as it did.
static const uint32_t DIM_MS[] = { 15000, 30000, 60000, 120000, 0 };   // Auto-dim, index 0..4; 0 = never
static int s_bright = 100;   // Brightness, percent 10..100
static int s_dim_i  = 2;     // index into DIM_MS
static const uint32_t SLEEP_MS[] = { 60000, 120000, 300000, 600000, 0 };   // Settings > Dark after, index 0..4; 0 = never dark
static int s_sleep_i = 2;    // index into SLEEP_MS
static int s_bl     = 100;   // last duty written; -1 forces the next tick to write (bsp_display_backlight_on wrote 100)
#if TT_CAL && (defined(BSP_HAS_AUDIO) || defined(BSP_HAS_HAPTIC))
static bool s_chime = true;  // Settings > Chime: gates the five-minutes-before ring in disk_tick_cb
#endif

// B8 + the OTA health mark: one 1s housekeeping timer, on the LVGL thread. Both jobs are the
// same shape - "once a second, cheaply, from a thread that is provably alive" - and a second
// timer for the second job would only be a second thing to get wrong.
static void disk_tick_cb(lv_timer_t *t)
{
    (void)t;
    // The backlight is this board's whole power budget: at 100% it is most of the draw, and a
    // disk that sits on a desk all afternoon spends nearly all of that lighting nobody. The
    // duty is written only on a CHANGE, because bsp_backlight_set reprograms the LEDC channel
    // and doing that every second for no reason is a visible flicker on the ST77916.
    uint32_t idle = trev_idle_ms();
    int want = s_bright;
    // Never dim through the end of a block. The last minute is the part you look up FOR, and
    // a screen that goes dark at 00:40 is a timer you stop trusting.
    if (!(pomodoist_countdown_critical())) {
        uint32_t dark = trev_dark_ms();
        if      (dark && idle > dark) want = 0;   // dark (trev_set_dark_ms, Settings > Dark after). The touch panel still scans, so any press wakes it.
        else if (DIM_MS[s_dim_i] && idle > DIM_MS[s_dim_i]) want = s_bright < 30 ? s_bright : 30;   // dim: readable across a desk, not across a room; never brighter than the setting
    }
#if TT_CAL && defined(BSP_HAS_HAPTIC)
    if (s_bl_restore) { s_bl_restore = false; s_bl = -1; }   // chime_flash forced 100: force the wanted level back out
#endif
    if (want != s_bl) { s_bl = want; bsp_backlight_set((uint8_t)want); }
#if TT_CAL && (defined(BSP_HAS_AUDIO) || defined(BSP_HAS_HAPTIC))
    // Five minutes before a timed event, once per event, face mounted or not. Same chime the
    // block-complete edge uses; the decision lives in cal_ui, the sound is this board's.
    // A4: cal_chime_due() runs first so the event is consumed either way; a clock nobody has
    // vouched for (RESTORED or NONE) must not ring for an event it may have mistimed.
    const bool chime_due = cal_chime_due();
    if (chime_due && s_chime && trev_net_time_state() == TREV_TIME_TRUSTED) {
        ESP_LOGI(TAG, "cal: T-5 chime (haptic %s)", bsp_haptic_enabled() ? "on" : "off");   // glass: did it fire, and was the motor allowed
#ifdef BSP_HAS_HAPTIC
        bsp_haptic_play(HAPTIC_THUNK);
        chime_flash();
#endif
    } else if (chime_due && s_chime && trev_net_time_state() != TREV_TIME_TRUSTED) {
        ESP_LOGW(TAG, "cal: T-5 chime held, clock not trusted");
    }
#endif
}



// A4, E5: trev_net waits 12 s for the first SNTP answer, once per connect. An answer that
// lands after that still sets the clock but would leave the state RESTORED/NONE, holding the
// chime all boot. This promotes it. Runs on the tcpip task; trev_net_trust_time only sets the
// clock and one state word, so it is safe there.
static void sntp_synced(struct timeval *tv)
{
    if (tv && trev_net_time_state() != TREV_TIME_TRUSTED) trev_net_trust_time(tv->tv_sec);
}

#if TT_CAL && defined(BSP_CLOCK_NVS) && !TT_CAL_SERIAL
// E5: Cal consumes time trust without depending on trev_net. A window fetched on an untrusted
// clock is held, and the Google response Date header can vouch for the clock once.
static bool cal_clock_trusted(void) { return trev_net_time_state() == TREV_TIME_TRUSTED; }
static void cal_http_date(const char *date)
{
    time_t t;
    if (trev_net_time_state() != TREV_TIME_TRUSTED && trev_http_date_parse(date, &t)) trev_net_trust_time(t);
}
#endif

// C2/C9: the wheel is sampled at 1 ms off this task (bsp_wheel.c) and drained here, on the LVGL
// task, so trev_input_wheel runs under the lock like every other trev_input_*. The queue holds 8,
// so one 8 ms drain never sees more than a burst.
static void wheel_drain_cb(lv_timer_t *t)
{
    (void)t;
    int d;
    while (bsp_wheel_pop(&d)) {
#ifdef BSP_HAS_HAPTIC
        if (trev_input_wheel(d)) bsp_haptic_play(HAPTIC_TICK);   // a wake-only detent (dark screen) gets no tick
#else
        trev_input_wheel(d);
#endif
    }
}

#ifdef BSP_HAS_HAPTIC
// Y5: thunk on a commit and on an app open. trev_open fires OPEN on EVERY open, the boot one
// included, so this is registered after the boot trev_open (see app_main), never before.
static void haptic_feedback_cb(trev_feedback_t fb)
{
    (void)fb;
    bsp_haptic_play(HAPTIC_THUNK);
}
#endif

// ---- the ring home's disc (D8, D9, D17, D18). trevos_home_ring.c asks; this is the board's answer.
// The disc is not the rail: no sync or pending marks here, only the clock's honesty, the
// battery when it is low, and what the selected app has to say for itself.

// D8: HH:MM once a clock source is vouched for (TRUSTED), ~HH:MM when it was only kept across a
// reset or restored from NVS, --:-- when nothing has landed. Under 15 percent a battery gauge adds " · N% LOW" (D9).
static void ring_status(char *line, size_t cap)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    trev_time_state_t ts = trev_net_time_state();
    char t[24];   // 24, not 6: -Wformat-truncation sizes %d at int's full range
    if (ts == TREV_TIME_NONE || tm.tm_year + 1900 < 2020) snprintf(t, sizeof t, "--:--");
    else if (ts == TREV_TIME_RESTORED) snprintf(t, sizeof t, "~%02d:%02d", tm.tm_hour, tm.tm_min);
    else snprintf(t, sizeof t, "%02d:%02d", tm.tm_hour, tm.tm_min);
    snprintf(line, cap, "%s", t);
}

static void ring_line_pomo(char *buf, size_t cap) { snprintf(buf, cap, "Focus + Todoist"); }
static void ring_line_settings(char *buf, size_t cap) { snprintf(buf, cap, "System"); }

// D17: what the calendar can honestly say about today. No link is "Not linked" whatever the
// cache holds; a sync older than 15 minutes, or one dated by a clock nobody vouches for, is
// stale; a link that has never landed a window says so rather than claiming a freshness.
static void ring_line_cal(char *buf, size_t cap)
{
    cal_link_t link = cal_sync_link();
    time_t last;
    if (link != CAL_LINK_OK) { snprintf(buf, cap, "Not linked"); return; }
    if (!cal_sync_last_sync(&last)) { snprintf(buf, cap, "Not synced yet"); return; }
    time_t now = time(NULL);
    if (trev_net_time_state() == TREV_TIME_NONE || now < last) { snprintf(buf, cap, "Today, stale"); return; }
    long min = (long)((now - last) / 60);
    if (min > 15) snprintf(buf, cap, "Today, stale");
    else if (min == 0) snprintf(buf, cap, "Today, synced just now");
    else snprintf(buf, cap, "Today, synced %ld min ago", min);
}

// D18: true when a wheel detent will be felt. Boards without the haptic never can, so they
// answer false and the ring flashes the selected segment instead.
static bool ring_haptics_on(void)
{
#ifdef BSP_HAS_HAPTIC
    return bsp_haptic_enabled();
#else
    return false;   // D18: no motor means no felt detent, so the ring flashes instead
#endif
}

#if TT_DEV_RING_FILL > 3
// Sim fixture: apps 4..TT_DEV_RING_FILL for the full-ring shot. Each is a name and nothing else,
// so it fills a segment and the disc can name it; opening one shows an empty face.
static void ring_line_fixture(char *buf, size_t cap) { snprintf(buf, cap, "Fixture"); }
#define RING_DUMMY(n) { .api_version = TREV_APP_API_VERSION, .id = "d" #n, .name = "Dummy " #n }
static const trev_app_def_t RING_FILL[] = { RING_DUMMY(4), RING_DUMMY(5), RING_DUMMY(6), RING_DUMMY(7), RING_DUMMY(8) };
#endif

#if TT_SETTINGS_ROWS
// ---- Settings rows (E14, C8). Prefs live in NVS namespace "trevset" as i32; keys are at most
// 15 characters (the NVS cap). Discrete sets are stored as an INDEX, so trev_pref_get's range
// check is a real validation. Every pref is read and applied in settings_init BEFORE the first
// face mounts; a row's `set` is the live apply path; settings_save (the face's idle hook) writes
// every scalar back, and NVS skips a primitive that did not change.
//   key        rows           range   default   map
//   bright     Brightness     10..100 100       percent
//   dim        Auto-dim       0..4    2         DIM_MS: 15, 30, 60, 120 s, never
//   sleep      Dark after     0..4    2         SLEEP_MS: 1, 2, 5, 10 min, never
//   haptics    Haptics        0..1    1         off / on
//   hstrength  Haptic strength 0..2   1         HSTR_FX: effect 1 strong, 7 light, 24 sharp
//   wheelinv   Wheel direction 0..1   0         normal / reversed
//   chime      Chime          0..1    1         off / on
#ifdef BSP_HAS_HAPTIC
static const uint8_t HSTR_FX[] = { 1, 7, 24 };
static int s_haptics = 1, s_hstr_i = 1;
#endif
static int s_wheel_inv;

static int  bri_get(void) { return s_bright; }
static void bri_set(int v) { s_bright = v; s_bl = v; bsp_backlight_set((uint8_t)v); }   // shown now; the tick will not write it back
static void bri_txt(char *b, size_t n) { snprintf(b, n, "%d%%", s_bright); }
static int  dim_get(void) { return s_dim_i; }
static void dim_set(int v) { s_dim_i = v; }
static void dim_txt(char *b, size_t n) { static const char *const T[] = { "15 s", "30 s", "60 s", "120 s", "Never" }; snprintf(b, n, "%s", T[s_dim_i]); }
static int  slp_get(void) { return s_sleep_i; }
static void slp_set(int v) { s_sleep_i = v; trev_set_dark_ms(SLEEP_MS[v]); }   // the one dark source (E6)
static void slp_txt(char *b, size_t n) { static const char *const T[] = { "1 min", "2 min", "5 min", "10 min", "Never" }; snprintf(b, n, "%s", T[s_sleep_i]); }
#ifdef BSP_HAS_HAPTIC
static int  hap_get(void) { return s_haptics; }
static void hap_set(int v) { s_haptics = v; bsp_haptic_set_enabled(v != 0); }   // one flag: silences tick, thunk and the chime motor; the ring reads it (D18)
static int  hst_get(void) { return s_hstr_i; }
static void hst_set(int v) { s_hstr_i = v; bsp_haptic_set_tick(HSTR_FX[v]); }
static void hst_txt(char *b, size_t n) { static const char *const T[] = { "Strong", "Light", "Sharp" }; snprintf(b, n, "%s", T[s_hstr_i]); }
#endif
static int  whl_get(void) { return s_wheel_inv; }
static void whl_set(int v) { s_wheel_inv = v; trev_set_wheel_invert(v != 0); }
static void whl_txt(char *b, size_t n) { snprintf(b, n, "%s", s_wheel_inv ? "Reversed" : "Normal"); }
#if defined(BSP_HAS_AUDIO) || defined(BSP_HAS_HAPTIC)
static int  chm_get(void) { return s_chime; }
static void chm_set(int v) { s_chime = v != 0; }
#endif

static void wifi_txt(char *b, size_t n)
{
    char ssid[33], ip[16];
    int rssi = 0;
    trev_net_status(ssid, sizeof ssid, &rssi, ip, sizeof ip);
    if (ip[0]) snprintf(b, n, "%d dBm  %s  %s", rssi, ssid, ip);   // dBm first: the face dots the tail, so the diagnostic number survives
    else       snprintf(b, n, "Not connected");
}


static void sync_txt(char *b, size_t n)
{
    time_t last, now = time(NULL);
    if (!cal_sync_last_sync(&last)) snprintf(b, n, "Never");
    else if (trev_net_time_state() != TREV_TIME_TRUSTED || now < last) snprintf(b, n, "--");
    else if (now - last < 60) snprintf(b, n, "Just now");
    else snprintf(b, n, "%ld min ago", (long)((now - last) / 60));   // ponytail: minutes only, a day-old sync reads 1440; hours if that ever matters
}

static void about_txt(char *b, size_t n) { snprintf(b, n, "%s", esp_app_get_description()->version); }
static void link_txt(char *b, size_t n)
{
    cal_link_t l = cal_sync_link();
    snprintf(b, n, "%s", l == CAL_LINK_NONE ? "Not linked" : l == CAL_LINK_EXPIRED ? "Expired" : TT_CAL_SERIAL ? "Laptop (USB)" : "In firmware");
}
static void a_clear_cal(void)
{
#if !TT_CAL_SERIAL   // serial has no firmware link to unlink (cal_sync.c: the laptop is the link)
    ESP_LOGI(TAG, "Calendar link is in the firmware. Reflash without secrets to unlink.");   // C7
#endif
    cal_sync_clear_cache();
}
static void a_restart(void) { ESP_LOGI(TAG, "settings: restart"); esp_restart(); }
static void a_reset(void) { ESP_LOGI(TAG, "settings: reset preferences"); trev_prefs_erase(); esp_restart(); }

// Static (the face keeps the pointer) and ordered group 0 before group 1 (the face does not sort).
static const trev_setting_row_t ROWS[] = {
    { .label = "Brightness", .kind = TREV_ROW_VALUE, .get = bri_get, .set = bri_set, .text = bri_txt, .min = 10, .max = 100, .step = 10 },
    { .label = "Auto-dim",   .kind = TREV_ROW_VALUE, .get = dim_get, .set = dim_set, .text = dim_txt, .min = 0, .max = 4, .step = 1 },
    { .label = "Dark after", .kind = TREV_ROW_VALUE, .get = slp_get, .set = slp_set, .text = slp_txt, .min = 0, .max = 4, .step = 1 },
#ifdef BSP_HAS_HAPTIC
    { .label = "Haptics",         .kind = TREV_ROW_TOGGLE, .get = hap_get, .set = hap_set },
    { .label = "Haptic strength", .kind = TREV_ROW_VALUE,  .get = hst_get, .set = hst_set, .text = hst_txt, .min = 0, .max = 2, .step = 1 },
#endif
    { .label = "Wheel direction", .kind = TREV_ROW_TOGGLE, .get = whl_get, .set = whl_set, .text = whl_txt },
    { .label = "Wi-Fi",     .kind = TREV_ROW_INFO,   .text = wifi_txt },
    { .label = "Last sync", .kind = TREV_ROW_INFO,   .text = sync_txt },
    { .label = "Sync now",  .kind = TREV_ROW_ACTION, .act = cal_sync_request_now },
#if defined(BSP_HAS_AUDIO) || defined(BSP_HAS_HAPTIC)
    { .label = "Chime",     .kind = TREV_ROW_TOGGLE, .get = chm_get, .set = chm_set },
#endif
    { .label = "About",                .kind = TREV_ROW_INFO,    .text = about_txt, .group = 1 },
    { .label = "Restart",              .kind = TREV_ROW_CONFIRM, .act = a_restart, .group = 1 },
    { .label = "Reset preferences",    .kind = TREV_ROW_CONFIRM, .act = a_reset, .group = 1 },
    { .label = "Clear calendar cache", .kind = TREV_ROW_CONFIRM, .act = a_clear_cal, .group = 1 },
    { .label = "Calendar link",        .kind = TREV_ROW_INFO,    .text = link_txt, .group = 1 },
};

// The face's idle hook: 1 s after the last change, on row close, and on stop.
static void settings_save(void)
{
    ESP_LOGI(TAG, "[settings] save");
    trev_pref_set("bright", s_bright);
    trev_pref_set("dim", s_dim_i);
    trev_pref_set("sleep", s_sleep_i);
#ifdef BSP_HAS_HAPTIC
    trev_pref_set("haptics", s_haptics);
    trev_pref_set("hstrength", s_hstr_i);
#endif
    trev_pref_set("wheelinv", s_wheel_inv);
#if defined(BSP_HAS_AUDIO) || defined(BSP_HAS_HAPTIC)
    trev_pref_set("chime", s_chime);
#endif
}

// Load every pref through the row's own `set` (the live apply path), then bind the rows and the
// save hook. Called under the LVGL lock, after trev_init and before any face is registered.
static void settings_init(void)
{
    dim_set(trev_pref_get("dim", 2, 0, 4));
    slp_set(trev_pref_get("sleep", 2, 0, 4));
    s_bright = trev_pref_get("bright", 100, 10, 100);   // written to the panel after bsp_display_backlight_on (app_main)
#ifdef BSP_HAS_HAPTIC
    hap_set(trev_pref_get("haptics", 1, 0, 1));
    hst_set(trev_pref_get("hstrength", 1, 0, 2));
#endif
    whl_set(trev_pref_get("wheelinv", 0, 0, 1));
#if defined(BSP_HAS_AUDIO) || defined(BSP_HAS_HAPTIC)
    chm_set(trev_pref_get("chime", 1, 0, 1));
#endif
    trev_settings_bind(ROWS, (int)(sizeof ROWS / sizeof ROWS[0]));
    trev_settings_set_idle_cb(settings_save);
}
#endif

void app_main(void)
{
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Boards with no battery-backed RTC: a clock kept across a reset, or else the last TRUSTED time
    // saved in NVS (read only when the live clock is unset), shown as "~HH:MM" (RESTORED) until
    // SNTP lands; see trev_net.h.
    trev_net_clock_restore();

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(bsp_display_init(&io, &panel));

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * 40,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,                 // 320 (landscape; panel swap_xy, gap stays 35,0)
        .vres = BSP_LCD_V_RES,                 // 170
        .monochrome = false,
        // Rotation owned HERE (lvgl_port), not in bsp. lvgl_port_add_disp calls
        // esp_lcd_panel_swap_xy/mirror at rotation-0 init using these values, so any
        // swap_xy set in bsp gets clobbered. swap_xy=true => hardware transpose =>
        // true landscape 320x170. mirror_y=true sets the upright 180deg orientation.
        .rotation = { .swap_xy = BSP_DISP_SWAP_XY, .mirror_x = BSP_DISP_MIRROR_X, .mirror_y = BSP_DISP_MIRROR_Y },
        .flags = { .buff_dma = true, .swap_bytes = BSP_DISP_SWAP_BYTES },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
#ifndef BSP_NO_DEV_SHOT
    tt_shot_attach(disp);   // dev: wrap flush cb so screenshots can tap panel bytes
#else
    (void)disp;
#endif

    pomodoist_ui_init();
    // Bangkok TZ, before any face is built. Moved up from beside the SNTP init (which stays
    // there, before Wi-Fi): the calendar's boot face asks "which day is today" at trev_open,
    // and cal_sync_start below parses the cache's generated stamp in local time.
    setenv("TZ", "ICT-7", 1);
    tzset();
    // Before trev_open (the boot face reads the cache this loads) and before Wi-Fi starts (it
    // registers its own GOT_IP handler).
#if defined(BSP_CLOCK_NVS) && !TT_CAL_SERIAL   // serial sets the clock itself (CALT), never via trev_net
    cal_sync_set_time_hooks(cal_clock_trusted, cal_http_date);   // other boards: NULL hooks, as before
#endif
#if TT_CAL_SERIAL
    cal_sync_start_serial();      // no token on the device: the laptop holds it and pushes
#elif defined(GCAL_REFRESH_TOKEN)
    ESP_LOGI(TAG, "GCAL_OAUTH_USB_ONLY"); // release script refuses LAN upload of credential-bearing images
    cal_sync_start_google(GCAL_CLIENT_ID, GCAL_CLIENT_SECRET, GCAL_REFRESH_TOKEN, GCAL_CAL_ID);
#elif defined(GCAL_URL)
    cal_sync_start(GCAL_URL, GCAL_CAL_ID);
#else
    cal_sync_start(NULL, NULL);   // secrets.h has no GCAL_URL: cache only, and say so
    ESP_LOGW(TAG, "GCAL_URL not in secrets.h; calendar shows the cache only");
#endif

    if (lvgl_port_lock(0)) {
        trev_init(lv_screen_active());
#if TT_SETTINGS_ROWS
        settings_init();            // every pref read + applied, rows bound, before any face mounts (C8); Dark after sets the one dark threshold (E6)
#else
        trev_set_dark_ms(300000);   // 5 min: the one dark threshold; disk_tick_cb and bsp_touch.c both read it (E6)
#endif
        trev_set_rail(pomodoist_rail_init, pomodoist_rail_update);   // the disk's rail (clock, pending, battery) for any face that asks
        // ADR-0015: home + Pomodoist + Cal. TT_BOOT_APP picks the boot face (default 2, Cal; the
        // puck sets 1, Pomodoist); home is one long press away.
        trev_app_register(&TREV_HOME_ROUND);   // 0 - home face (round)
        trev_app_register(&POMODOIST_APP);          // 1
        trev_app_register(&CAL_APP);           // 2 - Cal (boot face unless TT_BOOT_APP says otherwise)
        trev_app_register(&TREV_SETTINGS);     // 3 - Settings, pinned to 6 o'clock by the ring (D5)
#if TT_DEV_RING_FILL > 3
        for (int i = 0; i < TT_DEV_RING_FILL - 3 && i < (int)(sizeof RING_FILL / sizeof RING_FILL[0]); i++) {
            trev_app_register(&RING_FILL[i]);
            trev_ring_set_oneliner(RING_FILL[i].id, ring_line_fixture);
        }
#endif
        trev_set_home_app(0);
        trev_ring_set_status(ring_status);
        trev_ring_set_oneliner("pomodist", ring_line_pomo);
        trev_ring_set_oneliner("cal", ring_line_cal);
        trev_ring_set_oneliner("settings", ring_line_settings);
        trev_ring_set_haptics_on(ring_haptics_on);
#if TT_DEV_VIEW == 12
        trev_open(0);                          // shoot.sh: the home face
#elif TT_DEV_VIEW >= 1 && TT_DEV_VIEW <= 10
        trev_open(1);                          // dev-view: straight into pomodoist (forced face)
#else
        trev_open(TT_DEV_VIEW == 11 ? 2 : TT_BOOT_APP);   // TT_DEV_VIEW 11: the calendar; normal boot: the board's boot face
#endif
#ifndef BSP_NO_DEV_SHOT
        tt_shot_init();   // dev: stream periodic screenshots over USB serial
#endif
        // A1: one always-on 1s timer ticks the core regardless of the active face, so a
        // focus block keeps counting on the launcher. The core is ticked in exactly one place.
        pomodoist_ui_start();
        lv_timer_create(disk_tick_cb, 1000, NULL);   // B8 idle dimming + the OTA health mark
        trev_set_has_wheel(true);   // presence, not init success: the sim feeds the wheel from stdin
        if (bsp_wheel_init() == ESP_OK) lv_timer_create(wheel_drain_cb, 8, NULL);   // non-fatal: no wheel, touch only
        lvgl_port_unlock();
    }

    bsp_display_backlight_on();
#if TT_SETTINGS_ROWS && defined(BSP_HAS_BACKLIGHT_DIM)
    if (s_bright != 100) {   // backlight_on wrote 100; only a saved Brightness differs (a needless LEDC rewrite flickers)
        s_bl = s_bright;
        bsp_backlight_set((uint8_t)s_bright);
    }
#endif
#if BSP_INPUT_TOUCH
    bsp_touch_init();              // CYD: starts the touch poll task; it calls trev_input_* itself
#else
    bsp_buttons_init(on_btn, NULL);
#endif
#ifdef BSP_HAS_HAPTIC
    // After touch (the shared I2C bus exists by now) and after the boot trev_open above, so the
    // boot open does not thunk. Absent chip: no callback is registered and every play is a no-op.
    if (bsp_haptic_init() == ESP_OK) trev_set_feedback(haptic_feedback_cb);
#endif
    // Real wall-clock for the status bar (TZ is set above, before the faces). ORDER IS
    // LOAD-BEARING (T1, C1): the SNTP service must be initialised BEFORE trev_net_start, and
    // with autostart OFF: ESP_NETIF_SNTP_DEFAULT_CONFIG autostarts, firing before there is any
    // route. trev_net arms it on the first GOT_IP. main.c owns esp_netif_init, the event loop
    // and esp_netif_sntp_init; trev_net owns the STA netif and never calls any of the three.
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.start = false;
    sntp_cfg.sync_cb = sntp_synced;
    esp_netif_sntp_init(&sntp_cfg);

    // C1 order, every board, exactly once: Pomodoist first (it only registers its GOT_IP /
    // DISCONNECTED handler and starts waiting), then trev_net, so no event is missed. A board
    // whose secrets.h lists more than one network hands the whole ordered list over; it is
    // tried in order on every connect, so home stays preferred and the hotspot is only ever
    // reached where home is not. Boards that still carry the single pair get a list of one.
    if (!TT_CAL_SERIAL && BSP_WIFI_THIS_WAKE()) {
        pomodoist_sync_wifi_start(TODOIST_TOKEN);
#ifdef WIFI_NETWORKS
        static const trev_net_ap_t NETS[] = WIFI_NETWORKS;
#else
        static const trev_net_ap_t NETS[] = { { WIFI_SSID, WIFI_PASS } };
#endif
        trev_net_start(NETS, sizeof NETS / sizeof NETS[0]);
    }

    ESP_LOGI(TAG, "TrevOS up: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

}
