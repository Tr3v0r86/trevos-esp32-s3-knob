/* sim/stubs/sim_system.c: host esp_system / esp_timer / esp_app_desc for trevos_settings.c
 * (About facts, Restart). Round sims only (ROUND_ONLY): the puck and the disk are the only
 * targets that register the Settings face. esp_restart does NOT exit: the face logs
 * "settings: restart" before calling it and the shot must keep running. */
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include <time.h>

uint32_t esp_get_free_heap_size(void) { return 200 * 1024; }   /* fixed: shots stay repeatable */
void esp_restart(void) { }

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    static int64_t base = -1;   /* "boot" = first call, so Uptime starts near zero */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    if (base < 0) base = now;
    return now - base;
}

const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t d = { "sim" };
    return &d;
}
