#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DEVICE_WIFI_SSID_MAX       32
#define DEVICE_WIFI_PASSWORD_MAX   64
#define DEVICE_TAKO_API_KEY_MAX   192
#define DEVICE_TAKO_API_ID_MAX     23

#define DEVICE_REFRESH_MINUTES_DEFAULT  60
#define DEVICE_REFRESH_MINUTES_MIN       1
#define DEVICE_REFRESH_MINUTES_MAX   10080

typedef struct {
    char wifi_ssid[DEVICE_WIFI_SSID_MAX + 1];
    char wifi_password[DEVICE_WIFI_PASSWORD_MAX + 1];
    char tako_api_key[DEVICE_TAKO_API_KEY_MAX + 1];
    char tako_api_id[DEVICE_TAKO_API_ID_MAX + 1];
    uint32_t refresh_minutes;
} device_config_t;

void device_config_defaults(device_config_t *config);
bool device_config_is_valid(const device_config_t *config);
esp_err_t device_config_load(device_config_t *config);
esp_err_t device_config_save(const device_config_t *config);
esp_err_t device_config_save_api_id(const char *api_id);
