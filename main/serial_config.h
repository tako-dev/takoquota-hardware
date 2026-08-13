#pragma once

#include "device_config.h"
#include "esp_err.h"

/*
 * Serial/USB command-line configuration, an alternative to the BLE channel.
 *
 * Prints a small command set and applies edits to *config; on "save" the
 * configuration is validated and written to NVS. Returns ESP_OK once saved,
 * ESP_ERR_TIMEOUT if timeout_ms elapses (0 = wait forever).
 */
esp_err_t serial_config_run(device_config_t *config, uint32_t timeout_ms);

/* Ask a running serial_config_run() to stop (used when BLE saves first). */
void serial_config_abort(void);
