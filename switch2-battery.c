// SPDX-License-Identifier: GPL-2.0-or-later
#include "switch2.h"
#include "switch2-battery-state.h"
#include <linux/module.h>
#include <linux/jiffies.h>

/* Read-only capability flag for transports deciding whether to retain the
 * native battery stream alongside common input. */
static bool native_battery = true;
module_param(native_battery, bool, 0444);
MODULE_PARM_DESC(native_battery, "Native battery and battery-only side samples after common input");

static enum power_supply_property switch2_battery_properties[] = {
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_SCOPE,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_STATUS,
};

static int switch2_battery_get_property(struct power_supply *psy,
        enum power_supply_property property, union power_supply_propval *value)
{
    struct switch2_controller *ns2 = power_supply_get_drvdata(psy);
    unsigned long flags;
    bool valid;
    u8 power;
    static const int statuses[] = {
        [SWITCH2_BATTERY_UNKNOWN] = POWER_SUPPLY_STATUS_UNKNOWN,
        [SWITCH2_BATTERY_DISCHARGING] = POWER_SUPPLY_STATUS_DISCHARGING,
        [SWITCH2_BATTERY_CHARGING] = POWER_SUPPLY_STATUS_CHARGING,
        [SWITCH2_BATTERY_FULL] = POWER_SUPPLY_STATUS_FULL,
        [SWITCH2_BATTERY_NOT_CHARGING] = POWER_SUPPLY_STATUS_NOT_CHARGING,
    };

    spin_lock_irqsave(&ns2->battery_lock, flags);
    valid = ns2->battery_valid &&
            time_before(jiffies, ns2->battery_updated + 90 * HZ);
    power = ns2->battery_power;
    spin_unlock_irqrestore(&ns2->battery_lock, flags);

    switch (property) {
    case POWER_SUPPLY_PROP_PRESENT:
        value->intval = 1;
        break;
    case POWER_SUPPLY_PROP_SCOPE:
        value->intval = POWER_SUPPLY_SCOPE_DEVICE;
        break;
    case POWER_SUPPLY_PROP_CAPACITY:
        if (!valid)
            return -ENODATA;
        value->intval = switch2_battery_capacity(power);
        break;
    case POWER_SUPPLY_PROP_STATUS:
        value->intval = valid ? statuses[switch2_battery_status(power)] :
                                POWER_SUPPLY_STATUS_UNKNOWN;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

int switch2_battery_create(struct switch2_controller *ns2)
{
    struct power_supply_config config = { .drv_data = ns2 };
    struct power_supply *battery;
    unsigned long flags;

    /* Only the BLE side-report power field is established here. Register
     * before input devices so evdev consumers discover battery files on add. */
    /* The GATT transport is attached after HID probe, so inspect the HID bus
     * rather than ns2->transport at this point. */
    if (!ns2->hdev || ns2->hdev->bus != BUS_BLUETOOTH ||
        !switch2_controller_is_joycon(ns2->type))
        return 0;

    snprintf(ns2->battery_name, sizeof(ns2->battery_name), "switch2-%s-battery",
             dev_name(&ns2->hdev->dev));
    ns2->battery_desc = (struct power_supply_desc) {
        .name = ns2->battery_name,
        .type = POWER_SUPPLY_TYPE_BATTERY,
        .properties = switch2_battery_properties,
        .num_properties = ARRAY_SIZE(switch2_battery_properties),
        .get_property = switch2_battery_get_property,
    };
    battery = power_supply_register(&ns2->hdev->dev, &ns2->battery_desc, &config);
    if (IS_ERR(battery))
        return PTR_ERR(battery);
    spin_lock_irqsave(&ns2->battery_lock, flags);
    ns2->battery = battery;
    spin_unlock_irqrestore(&ns2->battery_lock, flags);
    return 0;
}

void switch2_battery_update(struct switch2_controller *ns2, u8 power)
{
    unsigned long flags;

    if (switch2_battery_capacity(power) < 0)
        return;
    power &= 0x3f;
    spin_lock_irqsave(&ns2->battery_lock, flags);
    if (!ns2->battery_valid || ns2->battery_power != power ||
        time_after_eq(jiffies, ns2->battery_updated + 90 * HZ)) {
        ns2->battery_power = power;
        ns2->battery_valid = true;
        /* power_supply_changed queues work; holding our lock keeps unregister
         * from racing this notification. No per-packet uevents for same state. */
        if (ns2->battery)
            power_supply_changed(ns2->battery);
    }
    ns2->battery_updated = jiffies;
    spin_unlock_irqrestore(&ns2->battery_lock, flags);
}

void switch2_battery_destroy(struct switch2_controller *ns2)
{
    struct power_supply *battery;
    unsigned long flags;

    spin_lock_irqsave(&ns2->battery_lock, flags);
    battery = ns2->battery;
    ns2->battery = NULL;
    ns2->battery_valid = false;
    ns2->common_input_active = false;
    spin_unlock_irqrestore(&ns2->battery_lock, flags);
    if (battery)
        power_supply_unregister(battery);
}
