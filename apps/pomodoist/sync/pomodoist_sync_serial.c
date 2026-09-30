// pomodoist_sync_serial.c — transport A device side.
//
// Reads newline-terminated JSON lines from the S3's USB-Serial/JTAG, parses the
// tasklist contract with cJSON, and hands the result to the app via a take()/flag.
// The latest pushed list wins; the app polls take() on its tick.
#include "pomodoist_sync.h"
#include "pomodoist_sync_internal.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR (no-op unless SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pomo_sync";
#define LINE_MAX 1400

// PSRAM .bss on the disk (D18 grew this to 21 KB and internal DRAM ran out for TLS and httpd); no-op elsewhere
EXT_RAM_BSS_ATTR static pomo_tasklist_t s_pending;
static bool s_have;
static SemaphoreHandle_t s_lock;

// Publish a parsed list into the slot the app drains. Shared by both transports.
void pomo_sync_publish(const pomo_tasklist_t *l)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pending = *l;
    s_have = true;
    xSemaphoreGive(s_lock);
}

static void parse_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) { ESP_LOGW(TAG, "bad json (%d bytes)", (int)strlen(line)); return; }

    static pomo_tasklist_t l;   // static: ~4KB off the serial RX task stack (single-threaded use)
    memset(&l, 0, sizeof l);
    cJSON *tasks = cJSON_GetObjectItem(root, "tasks");
    if (cJSON_IsArray(tasks)) {
        int n = cJSON_GetArraySize(tasks);
        if (n > POMO_MAX_TASKS) n = POMO_MAX_TASKS;
        for (int i = 0; i < n; i++) {
            cJSON *it = cJSON_GetArrayItem(tasks, i);
            cJSON *t  = cJSON_GetObjectItem(it, "t");
            cJSON *p  = cJSON_GetObjectItem(it, "p");
            cJSON *pm = cJSON_GetObjectItem(it, "n");
            cJSON *dn = cJSON_GetObjectItem(it, "d");
            if (cJSON_IsString(t))
                pomo_task_text_copy(l.task[l.count].title, sizeof(l.task[l.count].title), t->valuestring);
            if (cJSON_IsString(p))
                pomo_task_text_copy(l.task[l.count].project, sizeof(l.task[l.count].project), p->valuestring);
            l.task[l.count].pomos = cJSON_IsNumber(pm) ? (uint8_t)pm->valueint : 0;
            l.task[l.count].done  = cJSON_IsTrue(dn);
            l.count++;
        }
    }
    cJSON_Delete(root);

    pomo_sync_publish(&l);
    ESP_LOGI(TAG, "rx %d tasks", l.count);
}

static void rx_task(void *arg)
{
    (void)arg;
    static char line[LINE_MAX];
    int len = 0;
    uint8_t buf[128];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(200));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (len > 0) { line[len] = 0; parse_line(line); len = 0; }
            } else if (len < LINE_MAX - 1) {
                line[len++] = c;
            } else {
                len = 0;   // overflow: drop the partial line
            }
        }
    }
}

void pomodoist_sync_serial_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t e = usb_serial_jtag_driver_install(&cfg);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "driver install: %d", (int)e);
    xTaskCreate(rx_task, "pomo_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "transport A ready (push a JSON line over USB)");
}

bool pomodoist_sync_take(pomo_tasklist_t *out)
{
    bool got = false;
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_have) { *out = s_pending; s_have = false; got = true; }
    xSemaphoreGive(s_lock);
    return got;
}
