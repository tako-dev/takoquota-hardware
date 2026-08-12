#pragma once

#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

#define BLE_CONFIG_DEVICE_NAME "TAKO-EPAPER"

/* A timeout of zero keeps configuration mode active until settings are saved. */
esp_err_t ble_config_run(device_config_t *config, uint32_t timeout_ms);
