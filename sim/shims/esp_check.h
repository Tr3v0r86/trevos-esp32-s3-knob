// esp_check.h — sim shim. Only the one macro app_ledstrip.c uses.
#pragma once
#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_ERROR(x, tag, msg, ...) do {                      \
        esp_err_t _e_ = (x);                                            \
        if (_e_ != ESP_OK) {                                            \
            ESP_LOGE(tag, "%s: %d", msg, (int)_e_);                     \
            return _e_;                                                 \
        }                                                               \
    } while (0)
