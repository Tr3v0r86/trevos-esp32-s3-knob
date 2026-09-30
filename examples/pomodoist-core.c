#include "pomodoist_core.h"
#include <assert.h>
int main(void) {
    pomo_core_t core;
    pomo_core_init(&core);
    assert(!core.running && core.total_s > 0);
    pomo_core_toggle(&core);
    assert(core.running);
    pomo_core_tick(&core, 1000);
    pomo_core_tick(&core, 2000);
    assert(core.left_s == core.total_s - 1);
    return 0;
}
