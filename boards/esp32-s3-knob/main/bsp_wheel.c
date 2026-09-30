// bsp_wheel.c - see bsp_wheel.h. PCNT config is the padlano puck's encoder_init, unchanged;
// the sampling is what moved: a 1 ms esp_timer instead of an 8 ms LVGL timer.
#include "bsp_wheel.h"
#include <board_pins.h>
#include "wheel_decode.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "puck_bsp";

// PCNT high/low limits frame the hardware counter. This detented wheel pulses the count to +/-1
// per click then settles back to 0, so there is no counts-per-detent divisor to tune.
#define BSP_ENC_PCNT_HIGH_LIMIT  1000
#define BSP_ENC_PCNT_LOW_LIMIT  -1000
#define BSP_ENC_GLITCH_NS        1000
#define WHEEL_QUEUE_LEN          8

static pcnt_unit_handle_t s_pcnt;
static QueueHandle_t      s_q;
static int                s_prev;
static uint32_t           s_dropped;   // full-queue losses: counted, never logged

// Runs on the esp_timer task (default ESP_TIMER_TASK dispatch), so xQueueSend with a 0 timeout,
// not the FromISR variant. Nothing here blocks and nothing touches LVGL.
static void wheel_sample_cb(void *arg)
{
    (void)arg;
    int raw = 0;
    if (pcnt_unit_get_count(s_pcnt, &raw) != ESP_OK) return;
    int n = wheel_decode(&s_prev, raw);
    if (n == 0) return;
    int8_t d = (n > 0) ? 1 : -1;
    for (int i = 0; i < (n > 0 ? n : -n); i++) {
        if (xQueueSend(s_q, &d, 0) != pdTRUE) s_dropped++;   // full: the LVGL task is stalled; the detent is lost
    }
}

static esp_err_t wheel_bringup(void)
{
    s_q = xQueueCreate(WHEEL_QUEUE_LEN, sizeof(int8_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    pcnt_unit_config_t unit_cfg = {
        .high_limit = BSP_ENC_PCNT_HIGH_LIMIT,
        .low_limit  = BSP_ENC_PCNT_LOW_LIMIT,
        .flags.accum_count = true,
    };
    esp_err_t e = pcnt_new_unit(&unit_cfg, &s_pcnt);
    if (e != ESP_OK) return e;

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = BSP_ENC_GLITCH_NS };
    if ((e = pcnt_unit_set_glitch_filter(s_pcnt, &filter)) != ESP_OK) return e;

    // Two channels for full quadrature decode (x4): A counts on B-level, B counts on A-level.
    pcnt_chan_config_t cha_cfg = { .edge_gpio_num = PIN_ENC_A, .level_gpio_num = PIN_ENC_B };
    pcnt_channel_handle_t cha = NULL;
    if ((e = pcnt_new_channel(s_pcnt, &cha_cfg, &cha)) != ESP_OK) return e;
    if ((e = pcnt_channel_set_edge_action(cha, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                          PCNT_CHANNEL_EDGE_ACTION_INCREASE)) != ESP_OK) return e;
    if ((e = pcnt_channel_set_level_action(cha, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                           PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK) return e;

    pcnt_chan_config_t chb_cfg = { .edge_gpio_num = PIN_ENC_B, .level_gpio_num = PIN_ENC_A };
    pcnt_channel_handle_t chb = NULL;
    if ((e = pcnt_new_channel(s_pcnt, &chb_cfg, &chb)) != ESP_OK) return e;
    if ((e = pcnt_channel_set_edge_action(chb, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                          PCNT_CHANNEL_EDGE_ACTION_DECREASE)) != ESP_OK) return e;
    if ((e = pcnt_channel_set_level_action(chb, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                           PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK) return e;

    // Encoder lines need pull-ups for clean edges (reference uses 10K externals; add internal too).
    gpio_set_pull_mode(PIN_ENC_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_ENC_B, GPIO_PULLUP_ONLY);

    if ((e = pcnt_unit_enable(s_pcnt)) != ESP_OK) return e;
    if ((e = pcnt_unit_clear_count(s_pcnt)) != ESP_OK) return e;
    if ((e = pcnt_unit_start(s_pcnt)) != ESP_OK) return e;

    const esp_timer_create_args_t targs = { .callback = wheel_sample_cb, .name = "wheel1ms" };
    esp_timer_handle_t t = NULL;
    if ((e = esp_timer_create(&targs, &t)) != ESP_OK) return e;
    return esp_timer_start_periodic(t, 1000);
}

esp_err_t bsp_wheel_init(void)
{
    esp_err_t e = wheel_bringup();
    if (e != ESP_OK) ESP_LOGW(TAG, "wheel off (%s)", esp_err_to_name(e));   // E13: never abort
    return e;
}

bool bsp_wheel_pop(int *detent)
{
    int8_t d;
    if (!s_q || xQueueReceive(s_q, &d, 0) != pdTRUE) return false;
    *detent = d;
    return true;
}
