// trev_prefs.c — validated scalar NVS keys (C8). The NVS handle is opened per call and
// closed before returning, so none is ever held across an erase.
#include "trev_prefs.h"
#include "nvs.h"
#include "esp_log.h"

#define NS "trevset"
static const char *TAG = "trev_prefs";

int trev_pref_validate(int v, int def, int min, int max)
{
    return (v >= min && v <= max) ? v : def;
}

int trev_pref_get(const char *key, int def, int min, int max)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    int32_t v = 0;
    esp_err_t e = nvs_get_i32(h, key, &v);
    nvs_close(h);
    return e == ESP_OK ? trev_pref_validate((int)v, def, min, max) : def;
}

bool trev_pref_set(const char *key, int v)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_i32(h, key, (int32_t)v);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "set %s failed: %s", key, esp_err_to_name(e));
    return e == ESP_OK;
}

void trev_prefs_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_all(h) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}
