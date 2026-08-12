#pragma once

#include <stdint.h>

#include "device_config.h"
#include "esp_err.h"

typedef struct {
    double five_hour_used;
    double five_hour_limit;
    double five_hour_remaining;
    int five_hour_used_pct;
    uint32_t window_minutes;

    double weekly_used;
    double weekly_limit;
    double weekly_remaining;
    int weekly_used_pct;

    char fetched_at[20];
} tako_quota_t;

esp_err_t tako_fetch_quota(device_config_t *config, tako_quota_t *quota);
