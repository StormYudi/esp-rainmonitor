#pragma once

#include "esp_err.h"

#define RAINMONITOR_BLE_NAME "RainMonitor"

esp_err_t ble_link_init(void);
