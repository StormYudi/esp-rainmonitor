#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "esp_err.h"

typedef enum {
    LINK_TRANSPORT_NONE = 0,
    LINK_TRANSPORT_USB,
    LINK_TRANSPORT_BLE,
} link_transport_t;

typedef struct {
    float cpu;
    float gpu;
    float cpu_freq_mhz;
    float gpu_freq_mhz;
    float mem_pct;
    float mem_used_gb;
    float mem_total_gb;
    float net_up_kbs;
    float net_down_kbs;
    bool gpu_valid;
    bool freq_valid;
    bool gpu_freq_valid;
    bool mem_valid;
    bool net_valid;
    bool time_valid;
    bool fresh;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    char cpu_model[MODEL_NAME_LEN];
    char gpu_model[MODEL_NAME_LEN];
    float cores[MAX_CORES];
    uint8_t core_n;
} usb_sample_t;

esp_err_t usb_link_init(void);
void usb_link_ingest(const uint8_t *data, size_t len, link_transport_t src);
bool usb_link_take_sample(usb_sample_t *out);
bool usb_link_connected(void);
bool usb_link_bus_attached(void);
link_transport_t usb_link_transport(void);
