/* sim shim: esp_err.h */
#pragma once
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK    0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NO_FREE_PAGES     0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1101
#define ESP_ERR_NVS_NOT_FOUND         0x1102
#define ESP_ERROR_CHECK(x) do { esp_err_t _e_ = (x); assert(_e_ == ESP_OK); (void)_e_; } while (0)
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "esp_err"; }
