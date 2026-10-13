#pragma once

#include <stdint.h>

#include "esp_err.h"

// Owns the panel and two PSRAM frame slots. The renderer stages a frame band by
// band and presents it; a display task schedules transfers using the panel's TE
// signal while the next frame is drawn.

esp_err_t display_init(void);
void display_stage_band(int band_index, int x_start, int x_end, const uint16_t *pixels);
void display_present(void);
esp_err_t display_set_brightness(uint8_t level);
