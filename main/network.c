#include "network.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "network";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define WIFI_MAX_RETRIES   5

static EventGroupHandle_t s_events;
static esp_netif_t *s_station;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static int s_retries;
static bool s_initialized;
static bool s_sntp_initialized;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < WIFI_MAX_RETRIES) {
            s_retries++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retries = 0;
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t network_connect(const device_config_t *config)
{
    if (config == NULL || config->wifi_ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }

    s_station = esp_netif_create_default_wifi_sta();
    if (s_station == NULL) {
        err = ESP_FAIL;
        goto fail;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        goto fail;
    }
    s_initialized = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               event_handler, NULL,
                                               &s_wifi_handler);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               event_handler, NULL,
                                               &s_ip_handler);
    if (err != ESP_OK) {
        goto fail;
    }

    wifi_config_t wifi = { 0 };
    memcpy(wifi.sta.ssid, config->wifi_ssid, strlen(config->wifi_ssid));
    memcpy(wifi.sta.password, config->wifi_password,
           strlen(config->wifi_password));
    wifi.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi.sta.pmf_cfg.capable = true;
    wifi.sta.pmf_cfg.required = false;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        goto fail;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        err = ESP_ERR_TIMEOUT;
        goto fail;
    }

    ESP_LOGI(TAG, "connected to %s", config->wifi_ssid);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(err));
    network_stop();
    return err;
}

esp_err_t network_sync_time(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        return err;
    }
    s_sntp_initialized = true;

    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "system time synchronized");
    } else {
        ESP_LOGE(TAG, "time synchronization failed: %s", esp_err_to_name(err));
    }
    return err;
}

void network_stop(void)
{
    if (s_sntp_initialized) {
        esp_netif_sntp_deinit();
        s_sntp_initialized = false;
    }
    if (s_wifi_handler != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_handler);
        s_wifi_handler = NULL;
    }
    if (s_ip_handler != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_initialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_initialized = false;
    }
    if (s_station != NULL) {
        esp_netif_destroy_default_wifi(s_station);
        s_station = NULL;
    }
    if (s_events != NULL) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    s_retries = 0;
}
