/* sim shim: esp_netif.h */
#pragma once
#include "esp_err.h"
static inline esp_err_t esp_netif_init(void) { return ESP_OK; }
