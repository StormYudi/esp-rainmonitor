#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdbool.h>

typedef struct {
    bool tap;
    bool moved;
    int x;
    int y;
    int dy;
} touch_event_t;

esp_err_t touch_init(i2c_master_bus_handle_t bus);
bool touch_poll(touch_event_t *ev);
