#pragma once

#define LCD_H_RES 480
#define LCD_V_RES 480
#define BAND_ROWS 30
#define BAND_COUNT (LCD_V_RES / BAND_ROWS)

#define DISPLAY_BRIGHTNESS_PCT 70
#define LCD_PIXEL_CLOCK_HZ 60000000
#define LCD_QSPI_DRIVE_CAP GPIO_DRIVE_CAP_2

#define HISTORY_LEN 60
#define MAX_CORES 16
#define MODEL_NAME_LEN 40
#define SAMPLE_TIMEOUT_MS 3500
#define UI_POLL_MS 50
#define UI_IDLE_POLL_MS 100
#define UI_ZOOM_POLL_MS 16
#define UI_ANIM_MS 1
#define UI_REFRESH_MS 1000
#define UI_IDLE_REFRESH_MS 4000
#define BATT_POLL_MS 500
#define IDLE_DIM_LEVEL 48
#define IDLE_DIM_MS 8000
#define ZOOM_ANIM_MS 160
#define CORE_MIN_H 92
#define CORE_GAP 6

/* CO5300 QSPI wants RGB565 little-endian. Gray otherwise shows up purple. */
#define RGB565_BE(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define RGB(r, g, b) ((uint16_t)((RGB565_BE((r), (g), (b)) >> 8) | (RGB565_BE((r), (g), (b)) << 8)))

_Static_assert(LCD_V_RES % BAND_ROWS == 0 && BAND_ROWS % 2 == 0,
               "CO5300 bands require complete even row pairs");
