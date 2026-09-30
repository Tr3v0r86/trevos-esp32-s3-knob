// board_pins.h - Waveshare/Guition JC3636K518 "TAIJI_KNOB" puck, ESP32-S3 (display brain) pin map.
//
// ⚠️ REFERENCE ONLY (E9). NOTHING in this file is measured on this unit. Every pin comes from
// the padlano-puck header (the JC3636K518 schematic, ADR-0006), not from a probe, and the two
// I2C addresses below (CST816S 0x15, DRV2605 0x5A) are what the first-bringup I2C scan must
// confirm, not something already read. VERIFY EVERY PIN ON GLASS before trusting it. If the
// display does not come up or the scan is empty, the clone re-wired pins: re-derive first.
//
// Source: ../padlano-puck/firmware/components/bsp/include/board_pins.h. The macro names are the
// 1.85B disk's wherever the shared bsp_touch.c / bsp_i2c.c read them (PIN_LCD_PCLK, not _CLK),
// because those two files are compiled from boards/round-1-85b/main by path and include this
// header in angle brackets.
#pragma once

// --- Display: ST77916, 360x360 round, QSPI on SPI2 (reference) ---
#define PIN_LCD_CS    14
#define PIN_LCD_PCLK  13
#define PIN_LCD_D0    15
#define PIN_LCD_D1    16
#define PIN_LCD_D2    17
#define PIN_LCD_D3    18
#define PIN_LCD_RST   21     // active low
#define PIN_LCD_BL    47     // backlight, LEDC PWM into an AO3400A FET gate (LEDA tied 3V3)

#define LCD_H_RES    360
#define LCD_V_RES    360

// ESPHome JC3636W518V2 validated rate; 80MHz caused scanline shimmer on this panel.
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)

// RGB565 MSB-first on the wire on a little-endian S3: every pixel handed to draw_bitmap is
// byte-swapped (esp_lvgl_port's swap_bytes flag does it in the shell).
#define LCD_BIGENDIAN   1

// This ST77916 is the V2 bank and its init table omits INVON, so inversion is the caller's.
// Table and flag are a matched pair (same as the disk).
#define LCD_INVERT_COLOR 1

// --- Touch: CST816S on the I2C bus SHARED with the haptic (reference; expected at 0x15, unscanned) ---
#define PIN_TP_SDA    11
#define PIN_TP_SCL    12
#define PIN_TP_INT     9
#define PIN_TP_RST    10
#define I2C_ADDR_CST816S  0x15
// CST816S reset timing (community reverse-eng): RST HIGH 10ms / LOW 10ms / HIGH 50ms.

// --- Haptic: DRV2605 LRA on the same I2C bus (reference; expected at 0x5A, unscanned) ---
// EN tied 3V3 (always on), TRIG tied GND: fire over I2C only.
#define I2C_ADDR_DRV2605  0x5A

// --- Primary encoder: the wheel. ROTATION ONLY, no click (ADR-0004). (reference) ---
#define PIN_ENC_A      8
#define PIN_ENC_B      7

// Not carried from the padlano header: PIN_BATT_ADC 1 (battery unverified, E9; the puck omits
// BSP_HAS_BATTERY) and the audio-MCU UART on GPIO43/44 (the S3 owns no audio path and this repo
// never touches the U4WDH).
//
// There is no firmware-controllable peripheral power-enable on this board family: the 3V3 rail
// is always on. A peripheral that does not answer is a wrong pin or a held reset, not an
// unpowered rail.
