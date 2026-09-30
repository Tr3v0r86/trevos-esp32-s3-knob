// Host test for the wheel wake policy (C9, E6). No LVGL, no IDF.
#include "trev_wake.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(trev_wheel_action(1000, 300000) == TREV_WHEEL_TURN);
    assert(trev_wheel_action(300001, 300000) == TREV_WHEEL_WAKE);
    assert(trev_wheel_action(999999, 0) == TREV_WHEEL_TURN);   // 0 = never dark
    assert(trev_wheel_dir(1, false) == 1);
    assert(trev_wheel_dir(-3, false) == -1);
    assert(trev_wheel_dir(1, true) == -1);
    puts("PASS trev_wake");
    return 0;
}
