// bsp_i2c.h — the board's ONE I2C master bus.
//
// Six devices share GPIO10/11: CST816S touch, ES8311 codec, ES7210 mics, PCF85063 RTC,
// BQ27220 fuel gauge, QMI8658 IMU. i2c_new_master_bus() takes the port exclusively, so the
// second caller fails and the failure looks like "that chip is dead" rather than "someone
// already owns the bus". One owner, handed out lazily, is the whole fix.
#pragma once
#include "driver/i2c_master.h"

// Creates the bus on first call, returns the same handle after. NULL only if creation failed.
i2c_master_bus_handle_t bsp_i2c_bus(void);
