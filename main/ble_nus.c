#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_hosted.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_nus.h"

void ble_store_config_init(void);

static const char* TAG = "ble_nus";

static ble_nus_connection_cb_t s_conn_cb;
static ble_nus_rx_cb_t s_rx_cb;
static bool s_connected = false;
static uint16_t s_tx_attr_handle;
static uint16_t s_conn_handle;
static char s_device_name[16];

static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(NUS_SERVICE_UUID_BYTES);

static const ble_uuid128_t nus_rx_char_uuid =
    BLE_UUID128_INIT(NUS_RX_CHAR_UUID_BYTES);

static const ble_uuid128_t nus_tx_char_uuid =
    BLE_UUID128_INIT(NUS_TX_CHAR_UUID_BYTES);

static int ble_nus_gap_event(struct ble_gap_event *event, void *arg);
static void ble_nus_on_reset(int reason);
static void ble_nus_on_sync(void);
static void ble_nus_host_task(void *param);

static void
generate_device_name(void)
{
    ble_addr_t addr;
    if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr.val, NULL) == 0) {
        snprintf(s_device_name, sizeof(s_device_name), "Claude-%02X%02X",
                 addr.val[4], addr.val[5]);
    } else {
        strcpy(s_device_name, "Claude-0000");
    }
}

const char*
ble_nus_device_name(void)
{
    return s_device_name;
}

static int
nus_rx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t *data = malloc(len + 1);
    if (!data) return BLE_ATT_ERR_INSUFFICIENT_RES;

    ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL);
    data[len] = '\0';

    ESP_LOGI(TAG, "RX: %.*s", len, data);

    if (s_rx_cb) {
        s_rx_cb(data, len);
    }

    free(data);
    return 0;
}

static const struct ble_gatt_svc_def nus_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &nus_rx_char_uuid.u,
                .access_cb = nus_rx_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &nus_tx_char_uuid.u,
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                .val_handle = &s_tx_attr_handle,
            },
            {
                0,
            },
        },
    },
    {
        0,
    },
};

static void
ble_nus_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    fields.name = (uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;

    fields.uuids128 = (ble_uuid128_t *)&nus_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_nus_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising as %s", s_device_name);
}

static int
ble_nus_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                s_connected = true;
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Connected");
                if (s_conn_cb) s_conn_cb(true);
            }
        } else {
            ble_nus_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Disconnected");
        if (s_conn_cb) s_conn_cb(false);
        ble_nus_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_nus_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: conn_handle=%d attr_handle=%d curn=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {0};
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = event->passkey.params.numcmp;
            ESP_LOGI(TAG, "Passkey: %06" PRIu32, pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

static void
ble_nus_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void
ble_nus_on_sync(void)
{
    int rc;

    generate_device_name();

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "error ensuring address; rc=%d", rc);
        return;
    }

    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Device Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr_val[0], addr_val[1], addr_val[2],
                 addr_val[3], addr_val[4], addr_val[5]);
    }

    ble_nus_advertise();
}

static void
ble_nus_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void
ble_nus_set_connection_cb(ble_nus_connection_cb_t cb)
{
    s_conn_cb = cb;
}

void
ble_nus_set_rx_cb(ble_nus_rx_cb_t cb)
{
    s_rx_cb = cb;
}

esp_err_t
ble_nus_send(const uint8_t *data, uint16_t len)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "notify failed; rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t
ble_nus_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Connecting to C6 via SDIO...");
    rc = esp_hosted_connect_to_slave();
    if (rc != 0) {
        ESP_LOGE(TAG, "esp_hosted_connect_to_slave failed; rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SDIO link established");

    esp_hosted_coprocessor_fwver_t fwver;
    rc = esp_hosted_get_coprocessor_fwversion(&fwver);
    if (rc == 0) {
        ESP_LOGI(TAG, "C6 fw version: %u.%u.%u rev=%d pre=%d build=%d",
                 (unsigned)fwver.major1, (unsigned)fwver.minor1, (unsigned)fwver.patch1,
                 fwver.revision, fwver.prerelease, fwver.build);
    } else {
        ESP_LOGW(TAG, "Could not query C6 firmware version; rc=%d", rc);
    }

    uint32_t cp_chip_id = 0;
    char cp_name[32] = {0};
    rc = esp_hosted_get_cp_info(&cp_chip_id, cp_name, sizeof(cp_name));
    if (rc == 0) {
        ESP_LOGI(TAG, "C6 chip ID: 0x%04" PRIx32 ", target: %s", cp_chip_id, cp_name);
    }

    esp_hosted_app_desc_t app_desc;
    rc = esp_hosted_get_coprocessor_app_desc(&app_desc);
    if (rc == 0 && app_desc.magic_word == ESP_HOSTED_APP_DESC_MAGIC_WORD) {
        ESP_LOGI(TAG, "C6 app: %s v%s, IDF: %s, built: %s %s",
                 app_desc.project_name, app_desc.version,
                 app_desc.idf_ver, app_desc.date, app_desc.time);
    }

    ESP_LOGI(TAG, "Initializing BT controller on C6...");
    rc = esp_hosted_bt_controller_init();
    if (rc != 0) {
        ESP_LOGW(TAG, "esp_hosted_bt_controller_init failed; rc=%d", rc);
        ESP_LOGW(TAG, "C6 firmware does not support BT control over SDIO.");
        ESP_LOGW(TAG, "Continuing without BLE...");
        return ESP_FAIL;
    }

    rc = esp_hosted_bt_controller_enable();
    if (rc != 0) {
        ESP_LOGW(TAG, "esp_hosted_bt_controller_enable failed; rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BT controller on C6 enabled");

    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed; rc=%d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = ble_nus_on_reset;
    ble_hs_cfg.sync_cb = ble_nus_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(nus_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set("Claude-Buddy");
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed; rc=%d", rc);
    }

    ble_store_config_init();

    nimble_port_freertos_init(ble_nus_host_task);

    ESP_LOGI(TAG, "BLE NUS initialized");
    return ESP_OK;
}
