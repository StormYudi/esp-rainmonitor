#include "usb_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"

#ifdef __has_include
#if __has_include("tinyusb_default_config.h")
#include "tinyusb_default_config.h"
#endif
#endif

#ifndef CONFIG_TINYUSB_CDC_RX_BUFSIZE
#define CONFIG_TINYUSB_CDC_RX_BUFSIZE 512
#endif

static const char *TAG = "usb";
static SemaphoreHandle_t s_lock;
static usb_sample_t s_sample;
static char s_line[512];
static size_t s_line_len;
static int64_t s_last_ms;
static volatile bool s_host_open;
static link_transport_t s_source;

static const char *find_field(const char *line, const char *key)
{
    const size_t klen = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == line || p[-1] == ',') && p[klen] == '=') {
            return p + klen + 1;
        }
        p += klen;
    }
    return NULL;
}

static bool parse_float_field(const char *line, const char *key, float *out)
{
    const char *p = find_field(line, key);
    if (!p) {
        return false;
    }
    char *end = NULL;
    float value = strtof(p, &end);
    if (end == p) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_str_field(const char *line, const char *key, char *out, size_t n)
{
    const char *p = find_field(line, key);
    if (!p || n == 0) {
        return false;
    }
    size_t len = 0;
    if (*p == '"') {
        p++;
        while (p[len] && p[len] != '"') {
            len++;
        }
    } else {
        while (p[len] && p[len] != ',') {
            len++;
        }
    }
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, p, len);
    out[len] = 0;
    return len > 0;
}

static int parse_cores_field(const char *line, float *out, int max)
{
    const char *p = find_field(line, "cores");
    if (!p || max < 1) {
        return 0;
    }
    int n = 0;
    while (n < max && *p && *p != ',') {
        char *end = NULL;
        float value = strtof(p, &end);
        if (end == p) {
            break;
        }
        out[n++] = value;
        p = end;
        if (*p == ';') {
            p++;
        } else {
            break;
        }
    }
    return n;
}

static bool parse_time_field(const char *line, uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    char buf[16];
    if (!parse_str_field(line, "time", buf, sizeof(buf))) {
        return false;
    }
    int h = -1;
    int m = -1;
    int s = 0;
    if (sscanf(buf, "%d:%d:%d", &h, &m, &s) < 2) {
        return false;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        return false;
    }
    *hh = (uint8_t)h;
    *mm = (uint8_t)m;
    *ss = (uint8_t)s;
    return true;
}

static void apply_line_locked(char *line, link_transport_t src)
{
    float cpu = 0, gpu = 0;
    bool have_cpu = parse_float_field(line, "cpu", &cpu);
    bool have_gpu = parse_float_field(line, "gpu", &gpu);
    if (!have_cpu) {
        return;
    }

    float cpu_freq = 0, gpu_freq = 0, mem = 0, mem_used = 0, mem_total = 0, net_up = 0, net_down = 0;
    bool have_freq = parse_float_field(line, "cpu_freq", &cpu_freq);
    bool have_gpu_freq = parse_float_field(line, "gpu_freq", &gpu_freq);
    bool have_mem = parse_float_field(line, "mem", &mem);
    bool have_mem_used = parse_float_field(line, "mem_used", &mem_used);
    bool have_mem_total = parse_float_field(line, "mem_total", &mem_total);
    bool have_up = parse_float_field(line, "net_up", &net_up);
    bool have_down = parse_float_field(line, "net_down", &net_down);

    s_sample.cpu = cpu;
    s_sample.gpu = gpu;
    s_sample.gpu_valid = have_gpu;
    if (have_freq) {
        s_sample.cpu_freq_mhz = cpu_freq;
        s_sample.freq_valid = true;
    }
    if (have_gpu_freq) {
        s_sample.gpu_freq_mhz = gpu_freq;
        s_sample.gpu_freq_valid = true;
    }
    if (have_mem) {
        s_sample.mem_pct = mem;
        s_sample.mem_used_gb = mem_used;
        s_sample.mem_total_gb = mem_total;
        s_sample.mem_valid = have_mem_used && have_mem_total;
    }
    if (have_up || have_down) {
        s_sample.net_up_kbs = net_up;
        s_sample.net_down_kbs = net_down;
        s_sample.net_valid = true;
    }
    s_sample.fresh = true;
    s_source = src;
    uint8_t hh = 0, mm = 0, sec = 0;
    if (parse_time_field(line, &hh, &mm, &sec)) {
        s_sample.hour = hh;
        s_sample.minute = mm;
        s_sample.second = sec;
        s_sample.time_valid = true;
    }
    parse_str_field(line, "cpu_model", s_sample.cpu_model, sizeof(s_sample.cpu_model));
    parse_str_field(line, "gpu_model", s_sample.gpu_model, sizeof(s_sample.gpu_model));
    memset(s_sample.cores, 0, sizeof(s_sample.cores));
    s_sample.core_n = (uint8_t)parse_cores_field(line, s_sample.cores, MAX_CORES);
    s_last_ms = esp_timer_get_time() / 1000;
}

static void on_bytes(const uint8_t *data, size_t len, link_transport_t src)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_line[s_line_len] = 0;
            if (s_line_len > 0) {
                apply_line_locked(s_line, src);
            }
            s_line_len = 0;
            continue;
        }
        if (s_line_len + 1 < sizeof(s_line)) {
            s_line[s_line_len++] = c;
        } else {
            s_line_len = 0;
        }
    }
    xSemaphoreGive(s_lock);
}

void usb_link_ingest(const uint8_t *data, size_t len, link_transport_t src)
{
    if (data && len) {
        on_bytes(data, len, src);
    }
}

static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;
    if (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size) == ESP_OK && rx_size) {
        on_bytes(buf, rx_size, LINK_TRANSPORT_USB);
    }
}

static void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    (void)itf;
    s_host_open = event->line_state_changed_data.dtr;
}

esp_err_t usb_link_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "lock");
    memset(&s_sample, 0, sizeof(s_sample));

#ifdef TINYUSB_DEFAULT_CONFIG
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
#else
    const tinyusb_config_t tusb_cfg = {0};
#endif
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "tinyusb install");

    tinyusb_config_cdcacm_t acm_cfg = {0};
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    acm_cfg.callback_rx = &tinyusb_cdc_rx_callback;
    acm_cfg.callback_line_state_changed = &tinyusb_cdc_line_state_changed_callback;
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm_cfg), TAG, "cdc init");
    ESP_LOGI(TAG, "CDC ready");
    return ESP_OK;
}

bool usb_link_take_sample(usb_sample_t *out)
{
    bool fresh = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_sample.fresh) {
        *out = s_sample;
        s_sample.fresh = false;
        fresh = true;
    }
    xSemaphoreGive(s_lock);
    return fresh;
}

bool usb_link_connected(void)
{
    const int64_t now = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool live = s_last_ms && (now - s_last_ms) < SAMPLE_TIMEOUT_MS;
    xSemaphoreGive(s_lock);
    (void)s_host_open;
    return live;
}

bool usb_link_bus_attached(void)
{
    /* DTR drops when the cable or host CDC handle goes away. tud_connected()
     * / tud_mounted() stay true on this self-powered board without VBUS. */
    return s_host_open;
}

link_transport_t usb_link_transport(void)
{
    const int64_t now = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool live = s_last_ms && (now - s_last_ms) < SAMPLE_TIMEOUT_MS;
    const link_transport_t src = live ? s_source : LINK_TRANSPORT_NONE;
    xSemaphoreGive(s_lock);
    return src;
}
