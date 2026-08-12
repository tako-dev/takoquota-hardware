#include "tako_client.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "device_config.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "tako";

#define TAKO_BASE_URL "https://tako.shiroha.tech"
#define HTTP_RESPONSE_MAX 4096

typedef struct {
    char data[HTTP_RESPONSE_MAX];
    size_t length;
    bool overflow;
} http_response_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    size_t available = sizeof(response->data) - response->length - 1;
    size_t incoming = (size_t)event->data_len;
    if (incoming > available) {
        response->overflow = true;
        incoming = available;
    }
    if (incoming > 0) {
        memcpy(response->data + response->length, event->data, incoming);
        response->length += incoming;
        response->data[response->length] = '\0';
    }
    return ESP_OK;
}

static esp_err_t post_json(const char *path, const char *body, cJSON **result)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", TAKO_BASE_URL, path);

    http_response_t *response = calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS request failed: %s", esp_err_to_name(err));
        free(response);
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Tako returned HTTP %d", status);
        free(response);
        return ESP_FAIL;
    }
    if (response->overflow || response->length == 0) {
        ESP_LOGE(TAG, "Tako response is empty or too large");
        free(response);
        return ESP_ERR_INVALID_SIZE;
    }

    *result = cJSON_ParseWithLength(response->data, response->length);
    free(response);
    if (*result == NULL) {
        ESP_LOGE(TAG, "Tako returned invalid JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static bool json_number(const cJSON *object, const char *name, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        *value = item->valuedouble;
        return isfinite(*value);
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *end = NULL;
        double parsed = strtod(item->valuestring, &end);
        if (end != item->valuestring && *end == '\0' && isfinite(parsed)) {
            *value = parsed;
            return true;
        }
    }
    return false;
}

static bool valid_api_id(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    for (const char *p = value; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    return strtoull(value, NULL, 10) > 0;
}

static esp_err_t resolve_api_id(device_config_t *config)
{
    cJSON *request = cJSON_CreateObject();
    if (request == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(request, "apiKey", config->tako_api_key);
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *response = NULL;
    esp_err_t err = post_json("/apiStats/api/get-key-id", body, &response);
    cJSON_free(body);
    if (err != ESP_OK) {
        return err;
    }

    const cJSON *success = cJSON_GetObjectItemCaseSensitive(response, "success");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(response, "data");
    const cJSON *id = cJSON_IsObject(data)
                          ? cJSON_GetObjectItemCaseSensitive(data, "id")
                          : NULL;
    char api_id[DEVICE_TAKO_API_ID_MAX + 1] = { 0 };
    if (cJSON_IsString(id) && id->valuestring != NULL) {
        strlcpy(api_id, id->valuestring, sizeof(api_id));
    } else if (cJSON_IsNumber(id)) {
        snprintf(api_id, sizeof(api_id), "%.0f", id->valuedouble);
    }

    if (!cJSON_IsTrue(success) || !valid_api_id(api_id)) {
        ESP_LOGE(TAG, "Tako API key validation failed");
        err = ESP_ERR_INVALID_RESPONSE;
    } else {
        strlcpy(config->tako_api_id, api_id, sizeof(config->tako_api_id));
        err = device_config_save_api_id(config->tako_api_id);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Tako API key validated and user ID cached");
        }
    }

    cJSON_Delete(response);
    return err;
}

static int used_percent(double used, double limit)
{
    int value = (int)lround(used / limit * 100.0);
    if (value < 0) {
        return 0;
    }
    return value > 100 ? 100 : value;
}

static esp_err_t parse_quota(cJSON *root, tako_quota_t *quota)
{
    const cJSON *plan = cJSON_GetObjectItemCaseSensitive(root, "plan");
    const cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
    double window_minutes;

    if (!cJSON_IsObject(plan) || !cJSON_IsObject(usage) ||
        !json_number(usage, "windowCost", &quota->five_hour_used) ||
        !json_number(plan, "window_cost_limit", &quota->five_hour_limit) ||
        !json_number(plan, "window_minutes", &window_minutes) ||
        !json_number(usage, "weeklyCost", &quota->weekly_used) ||
        !json_number(plan, "weekly_cost_limit", &quota->weekly_limit) ||
        quota->five_hour_limit <= 0 || quota->weekly_limit <= 0 ||
        window_minutes <= 0 || window_minutes > UINT32_MAX) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    quota->window_minutes = (uint32_t)lround(window_minutes);
    quota->five_hour_remaining = fmax(0.0,
                                      quota->five_hour_limit - quota->five_hour_used);
    quota->weekly_remaining = fmax(0.0,
                                   quota->weekly_limit - quota->weekly_used);
    quota->five_hour_used_pct = used_percent(quota->five_hour_used,
                                             quota->five_hour_limit);
    quota->weekly_used_pct = used_percent(quota->weekly_used,
                                          quota->weekly_limit);

    time_t now;
    struct tm local;
    time(&now);
    localtime_r(&now, &local);
    strftime(quota->fetched_at, sizeof(quota->fetched_at), "%m-%d %H:%M", &local);
    return ESP_OK;
}

esp_err_t tako_fetch_quota(device_config_t *config, tako_quota_t *quota)
{
    if (!device_config_is_valid(config) || quota == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(quota, 0, sizeof(*quota));

    if (!valid_api_id(config->tako_api_id)) {
        esp_err_t err = resolve_api_id(config);
        if (err != ESP_OK) {
            return err;
        }
    }

    cJSON *request = cJSON_CreateObject();
    if (request == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(request, "apiId", config->tako_api_id);
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *response = NULL;
    esp_err_t err = post_json("/apiStats/api/user-quota", body, &response);
    cJSON_free(body);
    if (err == ESP_OK) {
        err = parse_quota(response, quota);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "quota fetched: 5h=%d%% weekly=%d%%",
                     quota->five_hour_used_pct, quota->weekly_used_pct);
        } else {
            ESP_LOGE(TAG, "unexpected quota response shape");
        }
    }
    cJSON_Delete(response);
    return err;
}
