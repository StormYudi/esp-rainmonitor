#include "battery.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_link.h"

#define BQ27220_ADDR 0x55
#define CMD_CONTROL 0x00
#define CMD_VOLTAGE 0x08
#define CMD_BATTERY_STATUS 0x0A
#define CMD_CURRENT 0x0C
#define CMD_REMAINING_CAP 0x10
#define CMD_FULL_CHARGE_CAP 0x12
#define CMD_STATE_OF_CHARGE 0x2C
#define CMD_OPERATION_STATUS 0x3A
#define CMD_DESIGN_CAPACITY 0x3C
#define CMD_SELECT_SUBCLASS 0x3E
#define CMD_MAC_DATA_SUM 0x60

#define CONTROL_DEVICE_NUMBER 0x0001
#define CONTROL_BAT_INSERT 0x000D
#define CONTROL_SEALED 0x0030
#define CONTROL_ENTER_CFG_UPDATE 0x0090
#define CONTROL_EXIT_CFG_UPDATE_REINIT 0x0091
#define CONTROL_EXIT_CFG_UPDATE 0x0092
#define UNSEALKEY1 0x0414
#define UNSEALKEY2 0x3672
#define FULLACCESSKEY 0xFFFF

#define STATUS_DSG (1U << 0)
#define STATUS_BATTPRES (1U << 3)
#define STATUS_FC (1U << 9)
#define OP_SEC_SHIFT 1
#define OP_SEC_MASK 0x6
#define OP_SEC_SEALED 3
#define OP_SEC_UNSEALED 2
#define OP_SEC_FULL 1
#define OP_CFGUPDATE (1U << 10)

#define CHARGE_MA 8
#define PACK_MV_MIN 3000
#define PACK_MV_MAX 4350
#define MOSAICO_MAH 80

/* QuotaFrame / ESP-Mosaico BSP: 80 mAh cell, fixed EDV, same DOD table. */
#define DM_SELF_DISCHARGE 0x9268
#define DM_NEAR_FULL 0x926B
#define DM_RESERVE_CAP 0x926D
#define DM_GAUGING_CFG 0x929B
#define DM_FULL_CHARGE_CAP 0x929D
#define DM_DESIGN_CAP 0x929F
#define DM_EMF 0x92A7
#define DM_C0 0x92A9
#define DM_R0 0x92AB
#define DM_T0 0x92AD
#define DM_R1 0x92AF
#define DM_TC 0x92B1
#define DM_C1 0x92B2
#define DM_EDV0 0x92B4
#define DM_EDV1 0x92B7
#define DM_EDV2 0x92BA
#define DM_DOD0 0x92BD

/* CCT | SC | FCC_LIM | FC_FOR_VDQ | IGNORE_SD */
#define MOSAICO_GAUGING_CFG 0x0D11

static const char *TAG = "battery";
static i2c_master_dev_handle_t s_dev;
static bool s_ready;
static bool s_charging;
static uint8_t s_pct_filt = 0xFF;

static esp_err_t read_u16(uint8_t cmd, uint16_t *out)
{
    uint8_t buf[2] = {0};
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &cmd, 1, buf, 2, 80), TAG, "gauge read 0x%02x", cmd);
    *out = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return ESP_OK;
}

static esp_err_t write_bytes(uint8_t cmd, const uint8_t *data, size_t len)
{
    uint8_t buf[36];
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = cmd;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, 80);
}

static esp_err_t control(uint16_t subcmd)
{
    uint8_t data[2] = {(uint8_t)subcmd, (uint8_t)(subcmd >> 8)};
    return write_bytes(CMD_CONTROL, data, 2);
}

static uint8_t checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return (uint8_t)(0xFF - sum);
}

static uint8_t op_sec(uint16_t op)
{
    return (uint8_t)((op & OP_SEC_MASK) >> OP_SEC_SHIFT);
}

static bool pack_voltage(uint16_t mv)
{
    return mv >= PACK_MV_MIN && mv <= PACK_MV_MAX;
}

static uint8_t soc_from_voltage(uint16_t mv)
{
    static const struct {
        uint16_t mv;
        uint8_t pct;
    } table[] = {
        {3300, 0},  {3480, 8},  {3600, 18}, {3680, 28}, {3720, 38},
        {3760, 48}, {3810, 58}, {3870, 68}, {3940, 78}, {4040, 88}, {4160, 100},
    };
    const int n = (int)(sizeof(table) / sizeof(table[0]));
    if (mv <= table[0].mv) {
        return 0;
    }
    if (mv >= table[n - 1].mv) {
        return 100;
    }
    for (int i = 1; i < n; ++i) {
        if (mv <= table[i].mv) {
            const int span = table[i].mv - table[i - 1].mv;
            const int dlt = table[i].pct - table[i - 1].pct;
            return (uint8_t)(table[i - 1].pct + ((int)mv - table[i - 1].mv) * dlt / span);
        }
    }
    return 100;
}

static uint8_t segments_for(uint8_t percent)
{
    if (percent >= 80) {
        return 4;
    }
    if (percent >= 50) {
        return 3;
    }
    if (percent >= 25) {
        return 2;
    }
    if (percent > 0) {
        return 1;
    }
    return 0;
}

static esp_err_t write_dm(uint16_t address, const uint8_t *data, uint8_t size)
{
    uint8_t block[34];
    block[0] = (uint8_t)address;
    block[1] = (uint8_t)(address >> 8);
    memcpy(block + 2, data, size);
    ESP_RETURN_ON_ERROR(write_bytes(CMD_SELECT_SUBCLASS, block, (size_t)size + 2), TAG, "dm stage 0x%04x", address);
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t commit[2] = {checksum(block, (size_t)size + 2), (uint8_t)(size + 4)};
    ESP_RETURN_ON_ERROR(write_bytes(CMD_MAC_DATA_SUM, commit, 2), TAG, "dm commit 0x%04x", address);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t write_dm_u16(uint16_t address, uint16_t value)
{
    uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return write_dm(address, data, 2);
}

static esp_err_t write_dm_u8(uint16_t address, uint8_t value)
{
    return write_dm(address, &value, 1);
}

static esp_err_t wait_cfgupdate(bool want)
{
    for (int i = 0; i < 100; ++i) {
        uint16_t op = 0;
        ESP_RETURN_ON_ERROR(read_u16(CMD_OPERATION_STATUS, &op), TAG, "op status");
        if (((op & OP_CFGUPDATE) != 0) == want) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t apply_mosaico_profile(void)
{
    uint16_t design = 0;
    if (read_u16(CMD_DESIGN_CAPACITY, &design) == ESP_OK && design == MOSAICO_MAH) {
        ESP_LOGI(TAG, "CEDV already %u mAh", design);
        return ESP_OK;
    }

    uint16_t op = 0;
    ESP_RETURN_ON_ERROR(read_u16(CMD_OPERATION_STATUS, &op), TAG, "op status");
    if (op_sec(op) == OP_SEC_SEALED) {
        ESP_RETURN_ON_ERROR(control(UNSEALKEY1), TAG, "unseal1");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(control(UNSEALKEY2), TAG, "unseal2");
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_RETURN_ON_ERROR(control(FULLACCESSKEY), TAG, "full1");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(control(FULLACCESSKEY), TAG, "full2");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_RETURN_ON_ERROR(read_u16(CMD_OPERATION_STATUS, &op), TAG, "op status");
    if (op_sec(op) != OP_SEC_FULL) {
        ESP_LOGW(TAG, "full access rejected SEC=%u", op_sec(op));
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(control(CONTROL_ENTER_CFG_UPDATE), TAG, "cfg enter");
    vTaskDelay(pdMS_TO_TICKS(1100));
    ESP_RETURN_ON_ERROR(wait_cfgupdate(true), TAG, "cfg enter wait");

    const uint16_t dod[] = {4147, 4002, 3969, 3938, 3880, 3824, 3794, 3753, 3677, 3574, 3490};
    esp_err_t err = ESP_OK;
    err = err ? err : write_dm_u8(DM_SELF_DISCHARGE, 20);
    err = err ? err : write_dm_u16(DM_NEAR_FULL, 5);
    err = err ? err : write_dm_u16(DM_RESERVE_CAP, 0);
    err = err ? err : write_dm_u16(DM_GAUGING_CFG, MOSAICO_GAUGING_CFG);
    err = err ? err : write_dm_u16(DM_FULL_CHARGE_CAP, MOSAICO_MAH);
    err = err ? err : write_dm_u16(DM_DESIGN_CAP, MOSAICO_MAH);
    err = err ? err : write_dm_u16(DM_EMF, 3670);
    err = err ? err : write_dm_u16(DM_C0, 115);
    err = err ? err : write_dm_u16(DM_R0, 968);
    err = err ? err : write_dm_u16(DM_T0, 4547);
    err = err ? err : write_dm_u16(DM_R1, 4764);
    err = err ? err : write_dm_u8(DM_TC, 11);
    err = err ? err : write_dm_u8(DM_C1, 0);
    err = err ? err : write_dm_u16(DM_EDV0, 3000);
    err = err ? err : write_dm_u16(DM_EDV1, 3410);
    err = err ? err : write_dm_u16(DM_EDV2, 3530);
    for (int i = 0; i < 11 && err == ESP_OK; ++i) {
        err = write_dm_u16((uint16_t)(DM_DOD0 + i * 2), dod[i]);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CEDV write failed: %s", esp_err_to_name(err));
        (void)control(CONTROL_EXIT_CFG_UPDATE);
        return err;
    }

    ESP_RETURN_ON_ERROR(control(CONTROL_EXIT_CFG_UPDATE_REINIT), TAG, "cfg exit");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_RETURN_ON_ERROR(wait_cfgupdate(false), TAG, "cfg exit wait");
    (void)control(CONTROL_SEALED);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "wrote QuotaFrame 80 mAh CEDV profile");
    return ESP_OK;
}

esp_err_t battery_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ27220_ADDR,
        .scl_speed_hz = 100000,
    };
    const esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BQ27220 attach failed: %s", esp_err_to_name(err));
        return err;
    }
    uint16_t probe = 0;
    if (read_u16(CMD_BATTERY_STATUS, &probe) != ESP_OK) {
        ESP_LOGW(TAG, "BQ27220 not responding");
        return ESP_ERR_NOT_FOUND;
    }
    if (apply_mosaico_profile() != ESP_OK) {
        ESP_LOGW(TAG, "CEDV profile not applied; SOC may stay 0/100 until voltage fallback");
    }
    if ((probe & STATUS_BATTPRES) == 0) {
        (void)control(CONTROL_BAT_INSERT);
    }
    s_ready = true;
    ESP_LOGI(TAG, "BQ27220 ready, status=0x%04x", probe);
    return ESP_OK;
}

battery_status_t battery_read(void)
{
    battery_status_t out = {0};
    if (!s_ready) {
        return out;
    }

    uint16_t flags = 0;
    uint16_t soc = 0;
    uint16_t mv = 0;
    uint16_t current_raw = 0;
    uint16_t rem = 0;
    uint16_t fcc = 0;
    if (read_u16(CMD_BATTERY_STATUS, &flags) != ESP_OK || read_u16(CMD_VOLTAGE, &mv) != ESP_OK ||
        read_u16(CMD_CURRENT, &current_raw) != ESP_OK || read_u16(CMD_STATE_OF_CHARGE, &soc) != ESP_OK) {
        return out;
    }
    (void)read_u16(CMD_REMAINING_CAP, &rem);
    (void)read_u16(CMD_FULL_CHARGE_CAP, &fcc);

    const int16_t current_ma = (int16_t)current_raw;
    const bool present = (flags & STATUS_BATTPRES) != 0;
    const bool dsg = (flags & STATUS_DSG) != 0;
    const bool fc = (flags & STATUS_FC) != 0;
    if (!present && !pack_voltage(mv)) {
        return out;
    }

    /* After the Mosaico CEDV profile is in RAM, trust the gauge like QuotaFrame.
     * If the chip is still on a default 650/1000 mAh profile, use voltage. */
    uint8_t percent;
    if (fcc >= 35 && fcc <= 120 && soc <= 100) {
        percent = (uint8_t)soc;
    } else if (soc > 0 && soc < 100) {
        percent = (uint8_t)soc;
    } else {
        percent = soc_from_voltage(mv);
    }
    if (s_pct_filt == 0xFF) {
        s_pct_filt = percent;
    } else {
        s_pct_filt = (uint8_t)((s_pct_filt * 2 + percent) / 3);
        percent = s_pct_filt;
    }

    /* Ignore DSG/FC and tud_connected(): RELAXATION leaves DSG clear, and
     * the S31 is self-powered so the USB PHY can stay "connected" after
     * unplug. +I or a live CDC DTR means Type-C is in; any real discharge
     * current forces white even if DTR is stale. Hold across ~0 mA so a
     * full pack stays green while the charger is still attached. */
    if (current_ma <= -CHARGE_MA) {
        s_charging = false;
    } else if (current_ma >= CHARGE_MA || usb_link_bus_attached()) {
        s_charging = true;
    }
    out.valid = true;
    out.percent = percent;
    out.charging = s_charging;
    out.segments = segments_for(out.percent);

    static int s_log;
    if ((s_log++ % 8) == 0) {
        ESP_LOGI(TAG, "V=%umV I=%dmA gauge=%u FCC=%u show=%u%% %s%s%s", mv, (int)current_ma, soc, fcc, out.percent,
                 out.charging ? "chg" : "dsg", dsg ? " DSG" : "", fc ? " FC" : "");
    }
    return out;
}
