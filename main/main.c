#include "battery.h"
#include "board.h"
#include "config.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ble_link.h"
#include "touch.h"
#include "ui.h"
#include "usb_link.h"

static const char *TAG = "rainmonitor";
static const uint8_t BRIGHTNESS_STEPS[] = {90, 140, 180, 230};
static int s_brightness = 1;
static int64_t s_disconnect_us;
static bool s_idle_dimmed;

static void apply_brightness(void)
{
    display_set_brightness(BRIGHTNESS_STEPS[s_brightness]);
    s_idle_dimmed = false;
}

static void update_idle_dim(bool connected)
{
    if (connected) {
        s_disconnect_us = 0;
        if (s_idle_dimmed) {
            apply_brightness();
        }
        return;
    }
    if (s_disconnect_us == 0) {
        s_disconnect_us = esp_timer_get_time();
    }
    if (!s_idle_dimmed && esp_timer_get_time() - s_disconnect_us >= (int64_t)IDLE_DIM_MS * 1000) {
        display_set_brightness(IDLE_DIM_LEVEL);
        s_idle_dimmed = true;
    }
}

void app_main(void)
{
    i2c_master_bus_handle_t i2c = NULL;
    ESP_ERROR_CHECK(board_init(&i2c));
    ESP_ERROR_CHECK(display_init());
    if (battery_init(i2c) != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge unavailable; icon will stay empty");
    }
    ESP_ERROR_CHECK(ui_init());
    apply_brightness();
    ESP_ERROR_CHECK(usb_link_init());
    if (ble_link_init() != ESP_OK) {
        ESP_LOGW(TAG, "bluetooth unavailable; USB only");
    }
    if (touch_init(i2c) != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable; tap-to-zoom disabled");
    }

    ESP_LOGI(TAG, "waiting for host samples over BLE or USB");
    ui_render(false, LINK_TRANSPORT_NONE, battery_read());
    ui_present();

    TickType_t last_draw = 0;
    TickType_t last_batt = 0;
    battery_status_t batt = battery_read();
    bool last_connected = false;
    link_transport_t last_transport = LINK_TRANSPORT_NONE;
    while (1) {
        bool dirty = false;
        if (board_take_boot()) {
            ui_clear_history();
            dirty = true;
        }
        if (board_take_ai()) {
            s_brightness = (s_brightness + 1) % 4;
            apply_brightness();
            if (!usb_link_connected()) {
                s_disconnect_us = esp_timer_get_time();
            }
        }

        usb_sample_t sample;
        while (usb_link_take_sample(&sample)) {
            ui_push_sample(&sample);
        }

        const bool connected = usb_link_connected();
        const link_transport_t transport = usb_link_transport();
        dirty = dirty || (connected != last_connected) || (transport != last_transport);
        last_connected = connected;
        last_transport = transport;
        update_idle_dim(connected);

        touch_event_t ev;
        if (touch_poll(&ev)) {
            if (ev.moved && ui_handle_scroll(ev.dy)) {
                dirty = true;
            }
            if (ev.tap && ui_handle_tap(ev.x, ev.y, connected)) {
                board_haptic_pulse();
                dirty = true;
            }
        }

        const bool anim = ui_animation_active();
        const TickType_t now = xTaskGetTickCount();
        if (now - last_batt >= pdMS_TO_TICKS(BATT_POLL_MS)) {
            battery_status_t next = battery_read();
            if (next.valid != batt.valid || next.charging != batt.charging || next.percent != batt.percent) {
                dirty = true;
            }
            batt = next;
            last_batt = now;
        }
        const uint32_t refresh_ms = connected ? UI_REFRESH_MS : UI_IDLE_REFRESH_MS;
        if (dirty || anim || (now - last_draw) >= pdMS_TO_TICKS(refresh_ms)) {
            ui_render(connected, transport, batt);
            ui_present();
            last_draw = now;
        }
        vTaskDelay(pdMS_TO_TICKS(ui_poll_ms(connected)));
    }
}
