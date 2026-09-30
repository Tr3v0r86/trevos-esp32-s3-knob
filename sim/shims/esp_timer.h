/* sim shim: esp_timer.h. esp_timer_get_time() is defined in stubs/sim_system.c. */
#pragma once
#include <stdint.h>

int64_t esp_timer_get_time(void);
