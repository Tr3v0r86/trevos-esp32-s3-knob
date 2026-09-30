// bsp_display.c - ST77916 QSPI bringup for the puck (JC3636K518, 360x360 round).
//
// Lifted from padlano-puck's bsp.c (display_init, backlight_init) with
// this file's names for the disk's contract: bsp_display_init / bsp_backlight_set /
// bsp_display_backlight_on. Rotation stays in the shared main.c (esp_lvgl_port MADCTL), so
// nothing here swaps or mirrors, and there is no bsp_display_set_flipped (no IMU seam here).
//
// ⚠️ HARDWARE-UNVERIFIED (E9): pins are schematic only, and the V2 init table is the one the
// padlano-puck bring-up used on this panel family.
#include "bsp_display.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_st77916.h"
// The puck and the 1.85B disk share the same ST77916 V2 init sequence (found via INCLUDE_DIRS).
#include "st77916_v2_init.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_disp";

#define LCD_SPI_HOST     SPI2_HOST
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_BITS   LEDC_TIMER_10_BIT

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_BITS,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "ledc timer");

    ledc_channel_config_t ccfg = {
        .gpio_num   = PIN_LCD_BL,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,          // dark until the first frame is on the panel
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "ledc channel");
    return ESP_OK;
}

esp_err_t bsp_backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t max = (1u << LEDC_DUTY_BITS) - 1u;
    uint32_t duty = (max * percent) / 100u;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty), TAG, "set_duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL), TAG, "update_duty");
    return ESP_OK;
}

void bsp_display_backlight_on(void)
{
    ESP_ERROR_CHECK(bsp_backlight_set(100));
}

esp_err_t bsp_display_init(esp_lcd_panel_io_handle_t *ret_io, esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    // QSPI bus: CLK + 4 data lines. max_transfer_sz is one 80-line partial buffer, as the disk:
    // the shell renders with partial draw buffers (main.c), never a full frame.
    const spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        LCD_H_RES * 80 * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    // QSPI: no separate D/C line, it is encoded in the 32-bit command. lvgl_port installs its
    // own transfer-done handling, so no color-trans-done callback here.
    esp_lcd_panel_io_spi_config_t io_cfg = ST77916_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, ret_io),
        TAG, "panel io");

    st77916_vendor_config_t vendor_cfg = {
        .init_cmds      = st77916_v2_init_cmds,
        .init_cmds_size = sizeof(st77916_v2_init_cmds) / sizeof(st77916_v2_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(*ret_io, &panel_cfg, ret_panel),
                        TAG, "new st77916");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*ret_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*ret_panel), TAG, "init");
    // This panel (Waveshare JC3636W518V2 family) needs colour inversion.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*ret_panel, LCD_INVERT_COLOR), TAG, "invert");
    // No gap. The ST77916 driver adds y_gap to every flushed RASET row and never swaps it under
    // the 90deg MADCTL rotation, so a 1px y_gap surfaced as a grey column on the right + a white
    // seam at a partial-buffer band boundary. x_gap=1 is worse (wraps past the 360-col edge ->
    // shear). The round bezel hides the panel's native 1px start, so (0,0) is correct here.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*ret_panel, 0, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*ret_panel, true), TAG, "disp on");

    ESP_LOGI(TAG, "ST77916 %dx%d QSPI up at %d MHz (V2 bank, %d cmds, invert=%d bigendian=%d)",
             LCD_H_RES, LCD_V_RES, LCD_PIXEL_CLOCK_HZ / 1000000,
             (int)(sizeof(st77916_v2_init_cmds) / sizeof(st77916_v2_init_cmds[0])),
             LCD_INVERT_COLOR, LCD_BIGENDIAN);
    return ESP_OK;
}
