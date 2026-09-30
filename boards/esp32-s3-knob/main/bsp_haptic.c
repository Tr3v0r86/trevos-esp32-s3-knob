// bsp_haptic.c - see bsp_haptic.h. The DRV2605 LRA sequence and the ROM playback are the padlano
// puck's haptic_init / bsp_haptic_play, copied unchanged; only the probe and the no-op gate are new.
#include "bsp_haptic.h"
#include <board_pins.h>
#include "bsp_i2c.h"
#include "esp_log.h"

static const char *TAG = "puck_bsp";

static i2c_master_dev_handle_t s_drv;             // NULL = absent (or init never ran): every call is a no-op
static bool                    s_enabled = true;
static uint8_t                 s_tick    = HAPTIC_TICK;

// Ignore errors: a missing/silent haptic must never stall the UI.
static void drv_w(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    i2c_master_transmit(s_drv, b, sizeof(b), 50);
}

esp_err_t bsp_haptic_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_bus();
    if (bus == NULL || i2c_master_probe(bus, I2C_ADDR_DRV2605, 50) != ESP_OK) {
        ESP_LOGW(TAG, "haptic absent");
        return ESP_ERR_NOT_FOUND;
    }
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_DRV2605,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev, &s_drv) != ESP_OK) {
        ESP_LOGW(TAG, "haptic absent");
        s_drv = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    // Auto-cal does not converge on this motor (padlano diag) and stalled boot ~1.5s: skip it and
    // drive open-loop, hard: max overdrive at a typical coin-LRA resonance (~200Hz; no datasheet).
    // HARDWARE-UNVERIFIED: assumes LRA; if it stays weak/buzzy it may be ERM (flip 0x1A 0xB6->0x36,
    // 0x1D 0xA0->0x20, LIBRARY 6->1).
    drv_w(0x01, 0x00);   // out of standby
    drv_w(0x1A, 0xB6);   // FEEDBACK_CTRL: N_ERM_LRA=1 (LRA), brake 3x, loop gain high
    drv_w(0x17, 0xFF);   // OD_CLAMP: max overdrive (strongest)
    drv_w(0x1D, 0xA1);   // CONTROL3: LRA open-loop (bit0=1)
    drv_w(0x20, 0x32);   // OL_LRA_PERIOD ~= 200 Hz
    drv_w(0x03, 0x06);   // LIBRARY_SELECTION: 6 = LRA
    drv_w(0x01, 0x00);   // MODE: internal-trigger ROM playback
    ESP_LOGI(TAG, "haptic: DRV2605 LRA open-loop @ ~200Hz, max overdrive");
    return ESP_OK;
}

void bsp_haptic_play(uint8_t effect)
{
    if (s_drv == NULL || !s_enabled) return;   // missing chip OR master gate off
    if (effect == HAPTIC_TICK) effect = s_tick;
    drv_w(0x04, effect);   // waveform sequencer slot 1 = effect
    drv_w(0x05, 0x00);     // slot 2 = terminator
    drv_w(0x0C, 0x01);     // GO: play now
}

void bsp_haptic_set_enabled(bool enabled) { s_enabled = enabled; }
bool bsp_haptic_enabled(void)             { return s_drv != NULL && s_enabled; }
void bsp_haptic_set_tick(uint8_t effect)  { s_tick = effect; }
