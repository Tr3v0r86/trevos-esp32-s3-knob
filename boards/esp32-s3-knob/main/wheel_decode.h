// wheel_decode.h - the puck wheel's leading-edge decode. Pure C, no IDF dependency, host-tested
// by test_wheel_decode.c.
#pragma once

// This detented wheel pulses the PCNT count to +/-1 per click then SETTLES BACK to 0 (verified on
// hardware, 37e8a68 / 03ae224 "confirmed both directions"). Fire once on the LEADING edge (prev 0
// -> nonzero); ignore the settle-back. A signed-delta decode fires + then - across one detent and
// NETS ZERO -> dead wheel. That regression shipped twice in the padlano firmware (4f91093, and
// again 539cc08 "encoder CW direction"). Returns the detents to emit (signed), else 0; *prev is
// the last raw count seen. If the raw count ever climbs monotonically (0,1,2,3..) the hardware is
// the accumulating type and this decode must change: settle that on the bench, not by guessing.
static inline int wheel_decode(int *prev, int raw)
{
    int fire = (*prev == 0 && raw != 0) ? raw : 0;
    *prev = raw;
    return fire;
}
