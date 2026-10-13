#include "ble_link.h"

#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "usb_link.h"

#include <string.h>

static const char *TAG = "ble";

/* 6b1a0001-7c21-4f3a-9b5e-2d8f0a1c4e70 */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x70, 0x4e, 0x1c, 0x0a, 0x8f, 0x2d, 0x5e, 0x9b, 0x3a, 0x4f, 0x21, 0x7c, 0x01, 0x00, 0x1a, 0x6b);
/* 6b1a0002-7c21-4f3a-9b5e-2d8f0a1c4e70 */
static const ble_uuid128_t s_rx_uuid =
    BLE_UUID128_INIT(0x70, 0x4e, 0x1c, 0x0a, 0x8f, 0x2d, 0x5e, 0x9b, 0x3a, 0x4f, 0x21, 0x7c, 0x02, 0x00, 0x1a, 0x6b);

static uint8_t s_own_addr_type;

static int gap_event(struct ble_gap_event *event, void *arg);

static int rx_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) {
        return 0;
    }
    uint8_t buf[512];
    if (len > sizeof(buf)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    usb_link_ingest(buf, len, LINK_TRANSPORT_BLE);
    return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &s_rx_uuid.u,
                    .access_cb = rx_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {0},
            },
    },
    {0},
};

static void start_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)RAINMONITOR_BLE_NAME;
    fields.name_len = strlen(RAINMONITOR_BLE_NAME);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv fields %d", rc);
        return;
    }

    static ble_uuid128_t uuid128;
    uuid128 = s_svc_uuid;
    struct ble_hs_adv_fields rsp = {0};
    rsp.uuids128 = &uuid128;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGW(TAG, "scan rsp %d", rc);
    }

    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    /* Default NimBLE interval is ~100 ms; 350 ms is still well inside the
     * host 4 s scan window and cuts advertising radio duty a lot. */
    adv.itvl_min = BLE_GAP_ADV_ITVL_MS(300);
    adv.itvl_max = BLE_GAP_ADV_ITVL_MS(400);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv start %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as %s", RAINMONITOR_BLE_NAME);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            start_advertise();
        } else {
            ESP_LOGI(TAG, "connected");
            ble_att_set_preferred_mtu(247);
            /* Samples arrive once a second; 50–100 ms events beat the
             * default ~15–30 ms empty-event cadence. */
            struct ble_gap_upd_params upd = {
                .itvl_min = 40,  /* 50 ms  (1.25 ms units) */
                .itvl_max = 80,  /* 100 ms */
                .latency = 0,
                .supervision_timeout = 400, /* 4 s  (10 ms units) */
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            int upd_rc = ble_gap_update_params(event->connect.conn_handle, &upd);
            if (upd_rc != 0) {
                ESP_LOGW(TAG, "conn update %d", upd_rc);
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected; reason=%d", event->disconnect.reason);
        start_advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertise();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGW(TAG, "ensure addr %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGW(TAG, "infer addr %d", rc);
        return;
    }
    start_advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "reset; reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_link_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_RETURN_ON_FALSE(ble_gatts_count_cfg(s_svcs) == 0, ESP_FAIL, TAG, "gatt count");
    ESP_RETURN_ON_FALSE(ble_gatts_add_svcs(s_svcs) == 0, ESP_FAIL, TAG, "gatt add");
    ESP_RETURN_ON_FALSE(ble_svc_gap_device_name_set(RAINMONITOR_BLE_NAME) == 0, ESP_FAIL, TAG, "name");
    ble_att_set_preferred_mtu(247);
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "GATT UART ready");
    return ESP_OK;
}
