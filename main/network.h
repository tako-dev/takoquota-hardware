#pragma once

#include "device_config.h"
#include "esp_err.h"

esp_err_t network_connect(const device_config_t *config);
esp_err_t network_sync_time(void);
void network_stop(void);
