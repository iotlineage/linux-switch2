/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../switch2-battery-state.h"

int main(void)
{
    const int expected[] = {0, 11, 22, 33, 44, 56, 67, 78, 89, 100};
    unsigned int level, flags, reserved;

    for (level = 0; level < 16; level++) {
        for (flags = 0; flags < 4; flags++) {
            for (reserved = 0; reserved < 4; reserved++) {
                unsigned char power = (level << 2) | flags | (reserved << 6);
                enum switch2_battery_status status = switch2_battery_status(power);
                assert(switch2_battery_capacity(power) ==
                       (level < 10 ? expected[level] : -1));
                if (level >= 10)
                    assert(status == SWITCH2_BATTERY_UNKNOWN);
                else if (flags & 2)
                    assert(status == SWITCH2_BATTERY_CHARGING);
                else if (flags & 1)
                    assert(status == (level == 9 ? SWITCH2_BATTERY_FULL :
                                     SWITCH2_BATTERY_NOT_CHARGING));
                else
                    assert(status == SWITCH2_BATTERY_DISCHARGING);
            }
        }
    }
    puts("PASS: all 256 power bytes, ten levels, charging and invalid states");
    return 0;
}
