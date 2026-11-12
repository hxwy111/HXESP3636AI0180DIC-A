#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"

#define DEVICE_NAME "HXESP3636-BLE"
#define MAX_RX_LEN 244

static const char *TAG = "HXESP3636_BLE";
static uint8_t own_addr_type;
static uint16_t tx_value_handle;
static bool tx_subscribed[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];
static uint32_t tick_counter;

static const ble_uuid128_t uart_service_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t uart_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t uart_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static int uart_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def uart_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uart_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &uart_rx_uuid.u,
                .access_cb = uart_gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &uart_tx_uuid.u,
                .access_cb = uart_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &tx_value_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void notify_connection(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "failed to allocate notification buffer");
        return;
    }

    int rc = ble_gatts_notify_custom(conn_handle, tx_value_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notification failed, conn=%u rc=%d", conn_handle, rc);
    }
}

static void notify_all(const uint8_t *data, uint16_t len)
{
    for (uint16_t conn_handle = 0;
         conn_handle < CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
         ++conn_handle) {
        if (tx_subscribed[conn_handle]) {
            notify_connection(conn_handle, data, len);
        }
    }
}

static int uart_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        attr_handle == tx_value_handle) {
        static const char ready[] = "HXESP3636 BLE ready";
        return os_mbuf_append(ctxt->om, ready, sizeof(ready) - 1) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t data[MAX_RX_LEN];
        uint16_t len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, data, sizeof(data), &len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        ESP_LOGI(TAG, "RX conn=%u len=%u", conn_handle, len);
        ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
        notify_all(data, len);
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void start_advertising(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status != 0) {
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            tx_subscribed[event->disconnect.conn.conn_handle] = false;
        }
        ESP_LOGI(TAG, "disconnected; reason=%d", event->disconnect.reason);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.conn_handle < CONFIG_BT_NIMBLE_MAX_CONNECTIONS &&
            event->subscribe.attr_handle == tx_value_handle) {
            tx_subscribed[event->subscribe.conn_handle] = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "TX notifications %s for conn=%u",
                     event->subscribe.cur_notify ? "enabled" : "disabled",
                     event->subscribe.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    default:
        return 0;
    }
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params params = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &uart_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set advertising fields: %d", rc);
        return;
    }

    /* The name plus a 128-bit UUID does not fit in one 31-byte ADV packet. */
    rsp_fields.name = (uint8_t *)DEVICE_NAME;
    rsp_fields.name_len = strlen(DEVICE_NAME);
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set scan response fields: %d", rc);
        return;
    }

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to start advertising: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as %s", DEVICE_NAME);
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to determine BLE address type: %d", rc);
        return;
    }
    start_advertising();
}

static void host_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void notify_task(void *arg)
{
    (void)arg;
    for (;;) {
        char message[32];
        int len = snprintf(message, sizeof(message), "tick=%lu",
                           (unsigned long)tick_counter++);
        notify_all((const uint8_t *)message, (uint16_t)len);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    ESP_ERROR_CHECK(ret);

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);
    rc = ble_gatts_count_cfg(uart_services);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(uart_services);
    assert(rc == 0);

    memset(tx_subscribed, 0, sizeof(tx_subscribed));
    nimble_port_freertos_init(host_task);
    xTaskCreate(notify_task, "ble_notify", 3072, NULL, 5, NULL);
}
