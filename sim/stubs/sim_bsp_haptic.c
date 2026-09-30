/* sim/stubs/sim_bsp_haptic.c: host bsp_haptic. No DRV2605: init succeeds (so main.c registers
 * the commit/open thunk callback, as it does on a board where the chip answers) and every play
 * is logged to stderr, the stream shoot.sh greps, so a shot can count feel events:
 *   [haptic] effect 7    one wheel detent (HAPTIC_TICK)
 *   [haptic] effect 1    a commit or an open (HAPTIC_THUNK)
 * Gate and tick override behave as on the device: disabled plays nothing, and HAPTIC_TICK plays
 * whatever bsp_haptic_set_tick last set. */
#include "bsp_haptic.h"
#include <stdio.h>

static bool s_enabled = true;
static uint8_t s_tick = HAPTIC_TICK;

esp_err_t bsp_haptic_init(void) { return ESP_OK; }

void bsp_haptic_play(uint8_t effect)
{
    if (!s_enabled) return;
    if (effect == HAPTIC_TICK) effect = s_tick;
    fprintf(stderr, "[haptic] effect %u\n", (unsigned)effect);
}

void bsp_haptic_set_enabled(bool enabled) { s_enabled = enabled; }

bool bsp_haptic_enabled(void) { return s_enabled; }   /* init always succeeds here, so the gate alone */

void bsp_haptic_set_tick(uint8_t effect) { s_tick = effect; }
