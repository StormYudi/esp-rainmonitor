#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t board_init(i2c_master_bus_handle_t *bus);
int board_lcd_clock_pin(void);
int board_lcd_reset_pin(void);
bool board_take_boot(void);
bool board_take_ai(void);
void board_haptic_pulse(void);
