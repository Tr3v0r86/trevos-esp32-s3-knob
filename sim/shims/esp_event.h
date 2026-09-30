/* sim shim: esp_event.h */
#pragma once
#include "esp_err.h"
static inline esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
