#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    bool valid;
    bool charging;
    uint8_t percent;
    uint8_t segments; // 0..4, QuotaFrame-style levels
} battery_status_t;

esp_err_t battery_init(i2c_master_bus_handle_t bus);
battery_status_t battery_read(void);
