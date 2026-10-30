/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SWITCH2_BATTERY_STATE_H
#define SWITCH2_BATTERY_STATE_H

/* Native side-report Power Info: external power, charging, then level 0..9.
 * Percentages are a coarse presentation of all ten states, not fuel-gauge
 * measurements. Reserved levels must not become a false full battery. */
static inline int switch2_battery_capacity(unsigned char power)
{
    unsigned int level = (power >> 2) & 0x0f;

    return level <= 9 ? (int)((level * 100 + 4) / 9) : -1;
}

enum switch2_battery_status {
    SWITCH2_BATTERY_UNKNOWN,
    SWITCH2_BATTERY_DISCHARGING,
    SWITCH2_BATTERY_CHARGING,
    SWITCH2_BATTERY_FULL,
    SWITCH2_BATTERY_NOT_CHARGING,
};

static inline enum switch2_battery_status
switch2_battery_status(unsigned char power)
{
    int capacity = switch2_battery_capacity(power);

    if (capacity < 0)
        return SWITCH2_BATTERY_UNKNOWN;
    if (power & 2)
        return SWITCH2_BATTERY_CHARGING;
    if (power & 1)
        return capacity == 100 ? SWITCH2_BATTERY_FULL :
                                SWITCH2_BATTERY_NOT_CHARGING;
    return SWITCH2_BATTERY_DISCHARGING;
}
#endif
