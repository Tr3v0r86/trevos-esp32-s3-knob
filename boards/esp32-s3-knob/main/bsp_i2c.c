// bsp_i2c.c — see bsp_i2c.h. Singleton because the port can only be claimed once.
#include "bsp_i2c.h"
#include <board_pins.h>   // angle brackets resolve via the including board's -I, so boards/puck compiles this file by path
#include "esp_log.h"

static const char *TAG = "bsp_i2c";
static i2c_master_bus_handle_t s_bus;

i2c_master_bus_handle_t bsp_i2c_bus(void)
{
    if (s_bus) return s_bus;

    i2c_master_bus_config_t cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus on SDA %d / SCL %d: %s", PIN_TP_SDA, PIN_TP_SCL, esp_err_to_name(e));
        s_bus = NULL;
    }
    return s_bus;
}
