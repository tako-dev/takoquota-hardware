#include "ble_config.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_config";

#define BLE_READY_BIT BIT0
#define BLE_SAVED_BIT BIT1
#define BLE_DISCONNECTED_BIT BIT2
#define BLE_SAVE_GRACE_MS 2000

typedef enum {
    FIELD_SSID = 1,
    FIELD_PASSWORD,
    FIELD_API_KEY,
    FIELD_INTERVAL,
    FIELD_COMMAND,
    FIELD_STATUS,
} config_field_t;

/* UUIDs: 7b1e0000..0006-b5a3-f393-e0a9-e50e24dcca9e. */
#define CONFIG_UUID(value) \
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                     0x93, 0xf3, 0xa3, 0xb5, (value), 0x00, 0x1e, 0x7b)

static const ble_uuid128_t s_service_uuid = CONFIG_UUID(0x00);
static const ble_uuid128_t s_ssid_uuid = CONFIG_UUID(0x01);
static const ble_uuid128_t s_password_uuid = CONFIG_UUID(0x02);
static const ble_uuid128_t s_api_key_uuid = CONFIG_UUID(0x03);
static const ble_uuid128_t s_interval_uuid = CONFIG_UUID(0x04);
static const ble_uuid128_t s_command_uuid = CONFIG_UUID(0x05);
static const ble_uuid128_t s_status_uuid = CONFIG_UUID(0x06);

static EventGroupHandle_t s_events;
static device_config_t s_pending;
static device_config_t *s_result;
static uint8_t s_own_addr_type;
static uint16_t s_status_handle;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_finished;
static char s_status[48];

void ble_store_config_init(void);

static int config_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_ssid_uuid.u,
                .access_cb = config_access,
                .arg = (void *)(intptr_t)FIELD_SSID,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_password_uuid.u,
                .access_cb = config_access,
                .arg = (void *)(intptr_t)FIELD_PASSWORD,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_api_key_uuid.u,
                .access_cb = config_access,
                .arg = (void *)(intptr_t)FIELD_API_KEY,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_interval_uuid.u,
                .access_cb = config_access,
                .arg = (void *)(intptr_t)FIELD_INTERVAL,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_command_uuid.u,
                .access_cb = config_access,
                .arg = (void *)(intptr_t)FIELD_COMMAND,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = config_access,
                .arg = (void *)(intptr_t)FIELD_STATUS,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void set_status(const char *status)
{
    strlcpy(s_status, status, sizeof(s_status));
    if (s_status_handle != 0) {
        ble_gatts_chr_updated(s_status_handle);
    }
}

static int append_value(struct os_mbuf *om, const void *value, size_t len)
{
    return os_mbuf_append(om, value, len) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int copy_text_value(struct os_mbuf *om, char *dest, size_t capacity,
                           bool allow_empty)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    if ((!allow_empty && len == 0) || len >= capacity) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (ble_hs_mbuf_to_flat(om, dest, capacity - 1, NULL) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (memchr(dest, '\0', len) != NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    dest[len] = '\0';
    return 0;
}

static int write_interval(struct os_mbuf *om)
{
    char value[12];
    int rc = copy_text_value(om, value, sizeof(value), false);
    if (rc != 0) {
        return rc;
    }
    for (const char *p = value; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            set_status("ERROR: INTERVAL");
            return BLE_ATT_ERR_UNLIKELY;
        }
    }

    unsigned long minutes = strtoul(value, NULL, 10);
    if (minutes < DEVICE_REFRESH_MINUTES_MIN ||
        minutes > DEVICE_REFRESH_MINUTES_MAX) {
        set_status("ERROR: INTERVAL");
        return BLE_ATT_ERR_UNLIKELY;
    }
    s_pending.refresh_minutes = (uint32_t)minutes;
    return 0;
}

static int save_pending_config(struct os_mbuf *om)
{
    char command[8];
    int rc = copy_text_value(om, command, sizeof(command), false);
    if (rc != 0) {
        return rc;
    }
    if (strcmp(command, "save") != 0 && strcmp(command, "SAVE") != 0) {
        set_status("ERROR: COMMAND");
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!device_config_is_valid(&s_pending)) {
        set_status("ERROR: MISSING FIELD");
        return BLE_ATT_ERR_UNLIKELY;
    }

    esp_err_t err = device_config_save(&s_pending);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save configuration: %s", esp_err_to_name(err));
        set_status("ERROR: SAVE FAILED");
        return BLE_ATT_ERR_UNLIKELY;
    }

    *s_result = s_pending;
    s_finished = true;
    set_status("SAVED");
    xEventGroupSetBits(s_events, BLE_SAVED_BIT);
    ESP_LOGI(TAG, "configuration saved (SSID=%s, interval=%" PRIu32 " min)",
             s_pending.wifi_ssid, s_pending.refresh_minutes);
    return 0;
}

static int config_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    config_field_t field = (config_field_t)(intptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (field) {
        case FIELD_SSID:
            return append_value(ctxt->om, s_pending.wifi_ssid,
                                strlen(s_pending.wifi_ssid));
        case FIELD_INTERVAL: {
            char value[12];
            int len = snprintf(value, sizeof(value), "%" PRIu32,
                               s_pending.refresh_minutes);
            return append_value(ctxt->om, value, (size_t)len);
        }
        case FIELD_STATUS:
            return append_value(ctxt->om, s_status, strlen(s_status));
        default:
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || s_finished) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    int rc;
    switch (field) {
    case FIELD_SSID:
        rc = copy_text_value(ctxt->om, s_pending.wifi_ssid,
                             sizeof(s_pending.wifi_ssid), false);
        break;
    case FIELD_PASSWORD:
        rc = copy_text_value(ctxt->om, s_pending.wifi_password,
                             sizeof(s_pending.wifi_password), true);
        break;
    case FIELD_API_KEY:
        rc = copy_text_value(ctxt->om, s_pending.tako_api_key,
                             sizeof(s_pending.tako_api_key), false);
        if (rc == 0) {
            s_pending.tako_api_id[0] = '\0';
        }
        break;
    case FIELD_INTERVAL:
        rc = write_interval(ctxt->om);
        break;
    case FIELD_COMMAND:
        return save_pending_config(ctxt->om);
    default:
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    if (rc == 0) {
        set_status("READY TO SAVE");
    }
    return rc;
}

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_LINK_ESTAB:
        if (event->connect.status == 0) {
            s_connection_handle = event->connect.conn_handle;
            xEventGroupClearBits(s_events, BLE_DISCONNECTED_BIT);
            ESP_LOGI(TAG, "configuration client connected");
        } else if (!s_finished) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        xEventGroupSetBits(s_events, BLE_DISCONNECTED_BIT);
        if (!s_finished) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_finished) {
            advertise();
        }
        return 0;
    default:
        return 0;
    }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)BLE_CONFIG_DEVICE_NAME;
    fields.name_len = strlen(BLE_CONFIG_DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set advertising data: %d", rc);
        return;
    }

    struct ble_hs_adv_fields response = { 0 };
    response.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    response.num_uuids128 = 1;
    response.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set scan response: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "failed to start advertising: %d", rc);
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to select BLE address: %d", rc);
        return;
    }
    advertise();
    xEventGroupSetBits(s_events, BLE_READY_BIT);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_config_run(device_config_t *config, uint32_t timeout_ms)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_pending = *config;
    s_result = config;
    s_finished = false;
    s_status_handle = 0;
    s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
    set_status(device_config_is_valid(config) ? "READY" : "NEEDS CONFIG");

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        vEventGroupDelete(s_events);
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_services);
    }
    if (rc == 0) {
        rc = ble_svc_gap_device_name_set(BLE_CONFIG_DEVICE_NAME);
    }
    if (rc != 0) {
        nimble_port_deinit();
        vEventGroupDelete(s_events);
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(host_task);

    EventBits_t ready = xEventGroupWaitBits(s_events, BLE_READY_BIT, pdFALSE,
                                             pdFALSE, pdMS_TO_TICKS(5000));
    if ((ready & BLE_READY_BIT) == 0) {
        err = ESP_ERR_TIMEOUT;
    } else {
        ESP_LOGI(TAG, "advertising as %s", BLE_CONFIG_DEVICE_NAME);
        TickType_t wait = timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
        EventBits_t bits = xEventGroupWaitBits(s_events, BLE_SAVED_BIT, pdFALSE,
                                               pdFALSE, wait);
        err = (bits & BLE_SAVED_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    s_finished = true;
    if (s_connection_handle != BLE_HS_CONN_HANDLE_NONE) {
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(BLE_SAVE_GRACE_MS));
        }
        ble_gap_terminate(s_connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        xEventGroupWaitBits(s_events, BLE_DISCONNECTED_BIT, pdFALSE, pdFALSE,
                            pdMS_TO_TICKS(1000));
    } else {
        ble_gap_adv_stop();
    }
    rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    } else if (err == ESP_OK) {
        err = ESP_FAIL;
    }

    vEventGroupDelete(s_events);
    s_events = NULL;
    return err;
}
