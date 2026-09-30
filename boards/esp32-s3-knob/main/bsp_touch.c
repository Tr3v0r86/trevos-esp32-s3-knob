// bsp_touch.c — CST816S capacitive touch bringup for the 1.85B.
//
// Structurally padlano-puck's touch_init() with the 1.85B's pins, same as bsp_display.c is
// its display path (thesis.md: the portable layer is the asset). Two deliberate differences:
//
//   - The puck sets swap_xy/mirror_y to match a 90 degree display rotation. We do not rotate,
//     so every flag starts at 0 and the quadrant test in main.c is what decides them.
//   - We pulse RST and scan the bus before creating the driver. The CST816S drops off I2C in
//     standby, so a cold scan can come back empty on a working panel; a reset wakes it. The
//     scan is also the only look we have had at the RTC, codec and IMU sitting on this bus.
//
// Standby is the load-bearing fact about this part and it shapes the read path below. An idle
// CST816S NACKs every transaction, so a plain 50Hz poll produces 50 I2C error blocks a second
// and buries the log it is supposed to be writing. INT is what gets us out of that: it fires
// on the first touch, and from there the chip is awake and answers ordinary polls until it
// reports the finger gone. So INT wakes, polling tracks. Neither alone is enough - INT pulses
// per report frame and a poll can sit between two of them, while polling alone hits standby.
#include "bsp_touch.h"
#include <board_pins.h>   // angle brackets resolve via the including board's -I, so boards/puck compiles this file by path
#include "driver/gpio.h"
#include "bsp_i2c.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if !BSP_BRINGUP
#include "esp_lvgl_port.h"
#include "trevos.h"
#include "trevos_ui.h"
#include "touch_drag.h"
#endif

static const char *TAG = "bsp_touch";

// Panel-to-glass transform (C10). Defaults are the disk's measured values (all four quadrants
// agree with the unrotated panel). A board whose display is rotated sets these as compile defs;
// the puck compiles this file by path with swap_xy and mirror_y.
#ifndef BSP_TOUCH_SWAP_XY
#define BSP_TOUCH_SWAP_XY 0
#endif
#ifndef BSP_TOUCH_MIRROR_X
#define BSP_TOUCH_MIRROR_X 0
#endif
#ifndef BSP_TOUCH_MIRROR_Y
#define BSP_TOUCH_MIRROR_Y 0
#endif

#define TOUCH_I2C_PORT     I2C_NUM_0
#define CST816S_REG_DIS_AUTOSLEEP  0xFE
#define TOUCH_I2C_FREQ_HZ  400000

static esp_lcd_touch_handle_t  s_tp;
static volatile bool s_irq;      // set by the INT ISR, cleared when we act on it
static bool s_awake;             // chip is out of standby, so the bus is safe to poll
static int s_x, s_y;             // last real coordinates, held across report gaps
static int64_t s_last_us;        // when the last cnt>0 frame arrived

// How long a press survives with no new frame before it counts as a release.
//
// This is not debounce, it is the CST816S's reporting model. The chip reports on CHANGE, so a
// finger held still stops producing frames while it is very much still on the glass. Treating
// each gap as a release chopped one held press into a burst of 40-80ms taps: held_ms never got
// past 180, the 700ms long press was unreachable, and HOME could not be performed at all.
// 100ms bridges the gaps and still releases within one poll of a real lift.
#define TOUCH_RELEASE_MS 100
#define TOUCH_DRAG_RELEASE_MS 300   // once a press has travelled, a pause is a pause, not a lift
// The tested capture helper owns the movement thresholds.
static bool s_dragging;             // set by touch_task once the press has travelled TOUCH_DRAG_PX

#if !BSP_BRINGUP
static void touch_task(void *arg);   // defined below, started by bsp_touch_init
#endif

static void IRAM_ATTR touch_isr(esp_lcd_touch_handle_t tp)
{
    (void)tp;
    s_irq = true;
}

// Reference addresses from the schematic, so the scan reads as findings instead of numbers.
// Anything unnamed here is a genuine surprise and worth chasing.
//
// Keep this table honest against docs/research/waveshare-esp32-s3-touch-lcd-1-85b.md, which is
// the schematic transcription, NOT against CONTEXT.md's peripherals prose. Building it from the
// prose is how 0x55 got reported as "not in the schematic" for a day: the BQ27220 was in the
// schematic table the whole time, and the research doc had already predicted the exact scan set
// 0x15, 0x18, 0x40, 0x51, 0x55, 0x6B that this board returns.
static const char *addr_name(uint8_t a)
{
    switch (a) {
    case I2C_ADDR_CST816S: return "CST816S touch";
    case 0x18:             return "ES8311 codec";
    case 0x40:             return "ES7210 mic array";
    case 0x51:             return "PCF85063 RTC";
    case 0x55:             return "BQ27220 fuel gauge";
    case 0x6A: case 0x6B:  return "QMI8658 IMU";
#ifdef I2C_ADDR_DRV2605
    case I2C_ADDR_DRV2605: return "DRV2605 haptic";   // the puck's board_pins.h; the disk defines none
#endif
    default:               return "UNKNOWN, not in the schematic";
    }
}

#if BSP_BRINGUP
// Dump the first 16 registers of a device we cannot name, so the scan produces evidence
// instead of a question mark. Bringup card only: the shell must not put speculative traffic
// on a shared bus at boot. What the bytes mean, roughly: a 24Cxx EEPROM returns flat 0xFF on
// a blank part and echoes whatever was written on a used one, a fuel gauge or sensor returns
// a fixed ID plus values that MOVE between two reads, and a dead address returns all zeros.
static void dump_unknown(uint8_t addr)
{
    i2c_master_dev_handle_t dev;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,
    };
    if (i2c_master_bus_add_device(bsp_i2c_bus(), &cfg, &dev) != ESP_OK) return;

    for (int pass = 0; pass < 2; pass++) {
        uint8_t reg = 0x00, buf[16] = {0};
        if (i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf), 200) == ESP_OK) {
            ESP_LOGI(TAG, "  0x%02x regs 0x00-0x0f pass %d: "
                     "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     addr, pass, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
                     buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
        } else {
            ESP_LOGW(TAG, "  0x%02x answered the scan but will not be read at register 0x00", addr);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));   // a live sensor's values move between passes
    }
    i2c_master_bus_rm_device(dev);
}
#endif

// ponytail: bringup probe. Delete once the bus map is recorded in CONTEXT.md.
static void bus_scan(void)
{
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(bsp_i2c_bus(), a, 50) == ESP_OK) {
            ESP_LOGI(TAG, "scan: 0x%02x  %s", a, addr_name(a));
            found++;
#if BSP_BRINGUP
            if (addr_name(a)[0] == 'U') dump_unknown(a);   // only a genuinely unnamed address
#endif
        }
    }
    if (!found) ESP_LOGW(TAG, "scan: nothing on the bus. Check SDA %d / SCL %d before blaming the driver",
                         PIN_TP_SDA, PIN_TP_SCL);
}

esp_err_t bsp_touch_init(void)
{
    // The bus is shared with the codec, the RTC, the IMU and the fuel gauge, so it is owned
    // by bsp_i2c rather than by whichever driver happens to come up first.
    i2c_master_bus_handle_t bus = bsp_i2c_bus();
    ESP_RETURN_ON_FALSE(bus, ESP_FAIL, TAG, "no i2c bus");

    // Wake the panel before scanning. Community-documented CST816S timing, same as the puck:
    // RST low 10ms, high 50ms. The driver resets again on create; doing it twice is harmless.
    gpio_config_t rst = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = BIT64(PIN_TP_RST) };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "rst gpio");
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    bus_scan();

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    io_cfg.scl_speed_hz = TOUCH_I2C_FREQ_HZ;
    // _Generic picks the v2 (bus-handle) factory for an i2c_master_bus_handle_t.
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &tp_io), TAG, "touch io");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .interrupt_callback = touch_isr,
        // MEASURED on glass 2026-08-29 by the quadrant test in main.c: all four quadrants
        // reported themselves, so the touch axes already agree with the unrotated panel and
        // every flag stays 0 (the BSP_TOUCH_* defaults above). The puck needs swap_xy/mirror_y
        // only because its display is rotated 90 degrees; it sets them as compile defs.
        .flags = { .swap_xy = BSP_TOUCH_SWAP_XY, .mirror_x = BSP_TOUCH_MIRROR_X, .mirror_y = BSP_TOUCH_MIRROR_Y },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_tp), TAG, "new cst816s");

    // Keep the controller awake. It auto-sleeps after a couple of idle seconds, and a finger
    // held perfectly still counts as idle, so a long hold could drop the chip off the bus
    // mid-gesture. This board is mains-powered on a desk, so the power the sleep saves buys
    // nothing and costs the escape gesture. Best-effort: a variant that ignores the register
    // is no worse off than before, because TOUCH_RELEASE_MS already covers the short gaps.
    const uint8_t awake = 0x01;
    if (esp_lcd_panel_io_tx_param(tp_io, CST816S_REG_DIS_AUTOSLEEP, &awake, 1) != ESP_OK)
        ESP_LOGW(TAG, "could not disable auto-sleep; long holds may still chop");

#if !BSP_BRINGUP
    xTaskCreate(touch_task, "touch", 3072, NULL, 4, NULL);
#endif
    ESP_LOGI(TAG, "CST816S up at 0x%02x on SDA %d / SCL %d (RST %d, INT %d), axes 1:1 with the panel",
             I2C_ADDR_CST816S, PIN_TP_SDA, PIN_TP_SCL, PIN_TP_RST, PIN_TP_INT);
    return ESP_OK;
}

// 180 flip, mechanism only - which way and when is a later task's call (the IMU decides).
//
// This is NOT a register write to the CST816S. Traced in the pinned managed_components:
// esp_lcd_touch_cst816s.c only wires get_xy (`cst816s->get_xy = esp_lcd_touch_cst816s_get_xy`),
// it never sets set_mirror_x/set_mirror_y, so esp_lcd_touch_set_mirror_x/_y() just records the
// flag (esp_lcd_touch.c: `tp->config.flags.mirror_x = mirror`) and, because tp->set_mirror_x
// is NULL, esp_lcd_touch_get_coordinates()'s generic software fallback does the actual flip on
// every read: `x[i] = tp->config.x_max - x[i]` (esp_lcd_touch.c). So the flip happens inside
// bsp_touch_read's call into the driver, not on the I2C bus - a debugger scoping the bus for a
// mirror-command write will find none, and that is expected, not a broken flip.
// That fallback is off by one at the mirrored edge (x_max - x, not x_max - 1 - x): a press at
// x=0 mirrors to x=LCD_H_RES, one past the last valid column, and a press at x=LCD_H_RES-1
// mirrors to x=1, not x=0. One pixel, harmless for zone/swipe math, but do not "fix" it here -
// it lives in the vendor component, and fixing it locally would double-flip the moment a
// future esp_lcd_touch update wires a real set_mirror_x for this chip.
// Idempotent either way - setting the same flag value twice is a no-op re-write.
void bsp_touch_set_flipped(bool flipped)
{
    if (!s_tp) return;
    esp_lcd_touch_set_mirror_x(s_tp, flipped);
    esp_lcd_touch_set_mirror_y(s_tp, flipped);
}

#if !BSP_BRINGUP
// ---------------------------------------------------------------------------
// Touch -> TrevOS verbs. Only in the shell build; the bringup card does its own polling.
// ---------------------------------------------------------------------------

// A long press is HOME, anywhere on the glass, and on this board it is the ONLY way out of a
// face: BOOT is on the back and unreachable in normal use. 700ms is the CYD's number on
// purpose - one gesture across boards, or it stops being muscle memory.

#define TOUCH_POLL_MS  20

// Dark = idle past trev_dark_ms(); 0 means the board never goes dark, so nothing is dark.
static bool touch_dark(uint32_t idle_ms)
{
    uint32_t dark = trev_dark_ms();
    return dark && idle_ms > dark;
}

// A swipe is a press that travelled: TOUCH_SWIPE_PX on either axis between touch-down and
// release, at any speed. Duration is not part of the test (see touch_task for why).

// Zones come from tt_zone_at(), never from display thirds. The bar is narrower than the glass
// on a round panel, so thirds miss the labels by 23px, and that is not cosmetic: a press on
// LENGTH lands in the old centre third and starts the timer. tt_actionbar draws with the same
// geometry, so the labels and their targets cannot drift.
static void apply_tap(int x, uint32_t held_ms)
{
    int zone = tt_zone_at(x);
    // Log the verb, not just the press. Without this a tap that does nothing is
    // indistinguishable from a tap that never arrived, and the two have completely
    // different causes: a dead zone map versus dead touch hardware.
    ESP_LOGI(TAG, "tap x=%d held=%lums -> %s", x, (unsigned long)held_ms,
             held_ms >= TOUCH_LONG_MS ? "HOME" : zone < 0 ? "PREV" : zone > 0 ? "NEXT" : "COMMIT");

    if (!lvgl_port_lock(0)) { ESP_LOGW(TAG, "lvgl lock refused, verb dropped"); return; }
    if (held_ms >= TOUCH_LONG_MS) {
        trev_input_home();
    } else {
        switch (zone) {
        case -1: trev_input_turn(TREV_TURN_PREV); break;
        case  1: trev_input_turn(TREV_TURN_NEXT); break;
        default: trev_input_commit();             break;
        }
    }
    lvgl_port_unlock();
}

static const char *gesture_name(trev_gesture_t g)
{
    switch (g) {
    case TREV_GESTURE_SWIPE_LEFT:  return "LEFT";
    case TREV_GESTURE_SWIPE_RIGHT: return "RIGHT";
    case TREV_GESTURE_SWIPE_DOWN:  return "DOWN";
    case TREV_GESTURE_SWIPE_UP:    return "UP";
    default:                       return "?";
    }
}

// D18: a plain tap that landed ABOVE the action bar is not one of the three verbs, it is a
// press on the face's content. Fire it as a gesture so the face can decide what its own middle
// means; a face with no use for it does nothing and the tap is a no-op, which is what it was
// before this existed. The bar's top edge comes from tt_actionbar_top(), the same shared
// geometry tt_zone_at hit-tests with, so content and chrome cannot drift apart.
//
// Coordinates arrive already flipped when the panel is upside down (bsp_touch_read applies the
// mirror), so "above the bar" is always above the bar as the user is holding it.
static void apply_content_tap(int x, int y, uint32_t held_ms)
{
    ESP_LOGI(TAG, "tap x=%d y=%d held=%lums -> CONTENT", x, y, (unsigned long)held_ms);
    if (!lvgl_port_lock(0)) { ESP_LOGW(TAG, "lvgl lock refused, gesture dropped"); return; }
    trev_input_content_tap(x, y);
    lvgl_port_unlock();
}

// A swipe fires trev_input_gesture instead of apply_tap; it never fires both for one press.
// Direction is read off whatever bsp_touch_read handed back, so a flipped panel (task 8's
// IMU decides when) already sees flipped coordinates here for free - "down" on the glass as
// the user holds it is always SWIPE_DOWN, without this function knowing the panel is upside down.
static void apply_swipe(int dx, int dy)
{
    trev_gesture_t g = (abs(dx) >= abs(dy))
        ? (dx > 0 ? TREV_GESTURE_SWIPE_RIGHT : TREV_GESTURE_SWIPE_LEFT)
        : (dy > 0 ? TREV_GESTURE_SWIPE_DOWN  : TREV_GESTURE_SWIPE_UP);

    ESP_LOGI(TAG, "swipe dx=%d dy=%d -> %s", dx, dy, gesture_name(g));

    if (!lvgl_port_lock(0)) { ESP_LOGW(TAG, "lvgl lock refused, gesture dropped"); return; }
    trev_input_gesture(g);
    lvgl_port_unlock();
}

static void touch_task(void *arg)
{
    (void)arg;
    bool was_down = false;
    touch_drag_t drag={0};
    int last_x = 0, last_y = 0;
    int down_x0 = 0, down_y0 = 0;   // press position, latched on the first frame of a press
    uint32_t held_ms = 0;
    uint32_t idle_at_down = 0;      // idle age at touch-down, latched before note_activity clears it

    for (;;) {
        int x = 0, y = 0;
        bool down = bsp_touch_read(&x, &y);
        if (down) {
            if (!was_down) {
                down_x0 = x; down_y0 = y; touch_drag_begin(&drag,x,y);
                // I3: a press on a dark panel must only WAKE it. The backlight does not come
                // back until the next disk_tick_cb, up to a second later, so firing a verb here
                // would start or pause a block the user cannot see. Latch the idle age BEFORE
                // note_activity resets it; release then swallows the verb. Second press acts.
                idle_at_down = trev_idle_ms();
            }
            if(was_down && !touch_dark(idle_at_down) && trev_has_direct_touch() &&
               touch_drag_ready(&drag,x,y)) {
                if(lvgl_port_lock(0)) {
                    if(trev_has_direct_touch()) {
                        trev_input_drag(x-drag.x,y-drag.y);
                        touch_drag_delivered(&drag,x,y);
                    }
                    lvgl_port_unlock();
                }
            }
            last_x = x; last_y = y;
            held_ms += TOUCH_POLL_MS;
            if (abs(x - down_x0) >= TOUCH_DRAG_PX || abs(y - down_y0) >= TOUCH_DRAG_PX) s_dragging = true;
            trev_input_note_activity();   // lv_tick_get() is lock-free, no lvgl lock needed
        } else if (was_down) {
            int dx = last_x - down_x0, dy = last_y - down_y0;
            bool was_drag = s_dragging;
            s_dragging = false;
            // Movement decides, not duration. Glass 2026-09-05: not one swipe was recognised in
            // a day of use because the old rule also demanded held_ms <= 400, and a deliberate
            // scroll drag on a 360px glass runs 500-800ms; those all fell through to content
            // taps and closed the card the user was trying to scroll. A finger that travelled
            // TOUCH_SWIPE_PX is a swipe however long it took, and a long press is a finger that
            // stayed put, so the two cannot be confused: HOME needs stillness, a swipe needs travel.
            touch_release_t release=touch_release(touch_dark(idle_at_down),drag.claimed,
                was_drag,dx,dy,held_ms,last_y<tt_actionbar_top() || trev_has_direct_touch());
            switch(release) {
            case TOUCH_WAKE: ESP_LOGI(TAG,"wake tap swallowed");break;
            case TOUCH_CONSUMED: case TOUCH_IGNORE: break;
            case TOUCH_SWIPE: apply_swipe(dx,dy);break;
            case TOUCH_CONTENT: apply_content_tap(last_x,last_y,held_ms);break;
            case TOUCH_ZONE: apply_tap(last_x,held_ms);break;
            }
            held_ms = 0;
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}
#endif  // !BSP_BRINGUP

bool bsp_touch_read(int *x, int *y)
{
    if (!s_tp) return false;

    // Idle and no interrupt: stay off the bus entirely, a standby CST816S NACKs everything.
    if (!s_awake && !s_irq) return false;
    s_irq = false;

    bool fresh = false;
    if (esp_lcd_touch_read_data(s_tp) == ESP_OK) {
        uint16_t tx = 0, ty = 0;
        uint8_t cnt = 0;
        if (esp_lcd_touch_get_coordinates(s_tp, &tx, &ty, NULL, &cnt, 1) && cnt > 0) {
            s_x = tx; s_y = ty;
            s_last_us = esp_timer_get_time();
            s_awake = true;
            fresh = true;
        }
    }

    // No frame this poll. That is a gap, not necessarily a lift, so hold the press briefly.
    // A drag gets three times the tolerance: the chip reports on change, so a finger that
    // pauses mid-scroll goes silent, and at 100ms that silence split one slow drag into a short
    // tap (closed the card) or a still hold (went HOME) on glass 2026-09-05. Taps keep 100ms so
    // a pill still fires within one poll of the lift.
    if (!fresh) {
        int64_t tol_us = (s_dragging ? TOUCH_DRAG_RELEASE_MS : TOUCH_RELEASE_MS) * 1000;
        if (s_awake && (esp_timer_get_time() - s_last_us) < tol_us) {
            fresh = true;           // still down, just between report frames
        } else {
            s_awake = false;        // really gone: stop polling, wait for the next interrupt
            return false;
        }
    }
    if (x) *x = s_x;
    if (y) *y = s_y;
    return true;
}
