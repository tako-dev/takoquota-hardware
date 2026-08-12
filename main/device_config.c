#include "device_config.h"

#include <string.h>

#include "nvs.h"

#define CONFIG_NAMESPACE "epaper_cfg"

void device_config_defaults(device_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->refresh_minutes = DEVICE_REFRESH_MINUTES_DEFAULT;
}

bool device_config_is_valid(const device_config_t *config)
{
    return config != NULL && config->wifi_ssid[0] != '\0' &&
           config->tako_api_key[0] != '\0' &&
           config->refresh_minutes >= DEVICE_REFRESH_MINUTES_MIN &&
           config->refresh_minutes <= DEVICE_REFRESH_MINUTES_MAX;
}

static esp_err_t load_string(nvs_handle_t handle, const char *key,
                             char *value, size_t capacity)
{
    size_t required = capacity;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        value[0] = '\0';
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
        value[0] = '\0';
        return ESP_OK;
    }
    return err;
}

esp_err_t device_config_load(device_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    device_config_defaults(config);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = load_string(handle, "ssid", config->wifi_ssid,
                      sizeof(config->wifi_ssid));
    if (err == ESP_OK) {
        err = load_string(handle, "password", config->wifi_password,
                          sizeof(config->wifi_password));
    }
    if (err == ESP_OK) {
        err = load_string(handle, "api_key", config->tako_api_key,
                          sizeof(config->tako_api_key));
    }
    if (err == ESP_OK) {
        err = load_string(handle, "api_id", config->tako_api_id,
                          sizeof(config->tako_api_id));
    }

    uint32_t minutes = DEVICE_REFRESH_MINUTES_DEFAULT;
    if (err == ESP_OK) {
        esp_err_t value_err = nvs_get_u32(handle, "refresh_min", &minutes);
        if (value_err == ESP_ERR_NVS_TYPE_MISMATCH) {
            minutes = DEVICE_REFRESH_MINUTES_DEFAULT;
        } else if (value_err != ESP_OK && value_err != ESP_ERR_NVS_NOT_FOUND) {
            err = value_err;
        }
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        config->refresh_minutes =
            minutes >= DEVICE_REFRESH_MINUTES_MIN &&
                    minutes <= DEVICE_REFRESH_MINUTES_MAX
                ? minutes
                : DEVICE_REFRESH_MINUTES_DEFAULT;
    }
    return err;
}

esp_err_t device_config_save(const device_config_t *config)
{
    if (!device_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, "ssid", config->wifi_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "password", config->wifi_password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "api_key", config->tako_api_key);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "api_id", config->tako_api_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, "refresh_min", config->refresh_minutes);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t device_config_save_api_id(const char *api_id)
{
    if (api_id == NULL || api_id[0] == '\0' ||
        strlen(api_id) > DEVICE_TAKO_API_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "api_id", api_id);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return err;
}
