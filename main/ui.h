#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "battery.h"
#include "esp_err.h"

#include "usb_link.h"

esp_err_t ui_init(void);
void ui_clear_history(void);
void ui_push_sample(const usb_sample_t *sample);
bool ui_handle_tap(int x, int y, bool connected);
bool ui_handle_scroll(int dy);
bool ui_animation_active(void);
int ui_poll_ms(bool connected);
void ui_render(bool connected, link_transport_t transport, battery_status_t battery);
void ui_present(void);
