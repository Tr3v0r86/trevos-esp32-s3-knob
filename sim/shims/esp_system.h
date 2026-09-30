/* sim shim: esp_system.h - the two calls trevos_settings.c makes (About facts, Restart). */
#pragma once
#include <stdint.h>

uint32_t esp_get_free_heap_size(void);
void esp_restart(void);   /* the sim returns (the real one does not); see stubs/sim_system.c */
