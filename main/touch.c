#include "touch.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TOUCH_INT GPIO_NUM_6
#define TAP_SLOP 28
#define LIFT_CONFIRM_MS 40
#define TAP_REFRACTORY_MS 220

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp;
static bool s_down;
static bool s_lifting;
static int s_down_x;
static int s_down_y;
static int s_last_x;
static int s_last_y;
static int64_t s_empty_us;
static int64_t s_last_tap_us;

esp_err_t touch_init(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_config, &io), TAG, "touch io");

    const esp_lcd_touch_config_t tp_config = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
    };
    const esp_err_t err = esp_lcd_touch_new_i2c_cst9220(io, &tp_config, &s_tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST9220 init failed: %s", esp_err_to_name(err));
        return err;
    }

    const gpio_config_t irq = {
        .pin_bit_mask = 1ULL << TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    (void)gpio_config(&irq);
    ESP_LOGI(TAG, "CST9220 ready");
    return ESP_OK;
}

bool touch_poll(touch_event_t *ev)
{
    if (!ev) {
        return false;
    }
    ev->tap = false;
    ev->moved = false;
    ev->x = 0;
    ev->y = 0;
    ev->dy = 0;
    if (!s_tp) {
        return false;
    }
    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) {
        return false;
    }

    esp_lcd_touch_point_data_t points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_tp, points, &count, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) != ESP_OK) {
        return false;
    }

    const int64_t now = esp_timer_get_time();
    if (count > 0) {
        s_lifting = false;
        const int x = points[0].x;
        const int y = points[0].y;
        if (!s_down) {
            s_down = true;
            s_down_x = x;
            s_down_y = y;
            s_last_x = x;
            s_last_y = y;
            return false;
        }
        ev->dy = y - s_last_y;
        s_last_x = x;
        s_last_y = y;
        if (ev->dy != 0) {
            ev->moved = true;
            return true;
        }
        return false;
    }

    if (!s_down) {
        return false;
    }
    if (!s_lifting) {
        s_lifting = true;
        s_empty_us = now;
        return false;
    }
    if (now - s_empty_us < (int64_t)LIFT_CONFIRM_MS * 1000) {
        return false;
    }

    s_down = false;
    s_lifting = false;
    const int dx = s_last_x - s_down_x;
    const int dy = s_last_y - s_down_y;
    if (dx * dx + dy * dy > TAP_SLOP * TAP_SLOP) {
        return false;
    }
    if (now - s_last_tap_us < (int64_t)TAP_REFRACTORY_MS * 1000) {
        return false;
    }
    s_last_tap_us = now;
    ev->tap = true;
    ev->x = s_down_x;
    ev->y = s_down_y;
    return true;
}
