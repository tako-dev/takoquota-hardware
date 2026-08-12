#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ble_config.h"
#include "board.h"
#include "device_config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "epd_1in54.h"
#include "epd_paint.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "nvs_flash.h"
#include "tako_client.h"

static const char *TAG = "main";

/* One 200x200 mono frame is 5000 bytes - fine as a static allocation. */
static uint8_t s_frame[EPD_BUF_SIZE];

static int right_x(int x_right, const char *text, int scale)
{
    return x_right - (int)strlen(text) * 8 * scale;
}

static void draw_header(const char *title)
{
    epd_paint_clear(s_frame, EPD_WHITE);
    epd_paint_fill_rect(s_frame, 0, 0, EPD_WIDTH, 30, EPD_BLACK);
    epd_paint_text_2x(s_frame, 8, 7, title, EPD_WHITE);
}

static void draw_centered(int y, const char *text, int scale)
{
    int x = (EPD_WIDTH - (int)strlen(text) * 8 * scale) / 2;
    epd_paint_text_scaled(s_frame, x < 0 ? 0 : x, y, text, EPD_BLACK, scale);
}

static esp_err_t display_frame(void)
{
    esp_err_t err = epd_init();
    if (err == ESP_OK) {
        err = epd_display(s_frame);
        if (err == ESP_OK) {
            err = epd_sleep();
        }
    }
    if (err != ESP_OK) {
        epd_power_off();
    }
    return err;
}

static void log_display_result(const char *screen, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to show %s screen: %s", screen,
                 esp_err_to_name(err));
    }
}

static esp_err_t show_setup_screen(void)
{
    draw_header("BLE SETUP");
    draw_centered(48, BLE_CONFIG_DEVICE_NAME, 1);
    epd_paint_rect(s_frame, 22, 72, 178, 127, EPD_BLACK);
    draw_centered(83, "PAIR WITH PHONE", 1);
    draw_centered(103, "WAITING...", 1);
    draw_centered(148, "CONFIGURE THEN SAVE", 1);
    draw_centered(172, "BLE STAYS ON", 1);
    return display_frame();
}

static esp_err_t show_error_screen(const char *reason)
{
    draw_header("TAKO ERROR");
    draw_centered(55, reason, 2);
    epd_paint_hline(s_frame, 24, 94, 152, EPD_BLACK);
    draw_centered(112, "LAST UPDATE FAILED", 1);
    draw_centered(145, "PRESS BOOT", 1);
    draw_centered(162, "FOR BLE SETUP", 1);
    return display_frame();
}

static void format_quota_number(double value, char *output, size_t capacity)
{
    if (value >= 9999.5) {
        snprintf(output, capacity, "9999+");
    } else {
        snprintf(output, capacity, "%.1f", value);
    }
}

static void format_limit(double value, char *output, size_t capacity)
{
    if (value < 10000.0) {
        snprintf(output, capacity, "/ %.0f", value);
    } else {
        snprintf(output, capacity, "/ %.1fk", value / 1000.0);
    }
}

static void draw_quota_row(int y0, const char *label, const char *number,
                           const char *limit, int used_pct)
{
    const int x0 = 8;
    const int x1 = EPD_WIDTH - 8;

    epd_paint_text(s_frame, x0, y0, label, EPD_BLACK);

    char percent[12];
    snprintf(percent, sizeof(percent), "USED %d%%", used_pct);
    epd_paint_text(s_frame, right_x(x1, percent, 1), y0, percent, EPD_BLACK);

    int number_scale = strlen(number) <= 5 ? 3 : 2;
    int nx = epd_paint_text_scaled(s_frame, x0, y0 + 13, number,
                                   EPD_BLACK, number_scale);
    int baseline = y0 + 13 + number_scale * 8 - 8;
    epd_paint_text(s_frame, nx + 4, baseline, limit, EPD_BLACK);

    const int bar_y = y0 + 44;
    const int bar_h = 14;
    epd_paint_fill_rect(s_frame, x0, bar_y, x1, bar_y + bar_h, EPD_BLACK);
    epd_paint_fill_rect(s_frame, x0 + 2, bar_y + 2, x1 - 2,
                        bar_y + bar_h - 2, EPD_WHITE);
    int inner_w = (x1 - 2) - (x0 + 2);
    int remaining_pct = 100 - used_pct;
    int fill_w = inner_w * remaining_pct / 100;
    epd_paint_fill_rect(s_frame, x0 + 2, bar_y + 2,
                        x0 + 2 + fill_w, bar_y + bar_h - 2, EPD_BLACK);
}

static esp_err_t show_quota_screen(const tako_quota_t *quota)
{
    char window_label[24];
    if (quota->window_minutes % 60 == 0) {
        snprintf(window_label, sizeof(window_label), "%" PRIu32 "H WINDOW",
                 quota->window_minutes / 60);
    } else {
        snprintf(window_label, sizeof(window_label), "%" PRIu32 "M WINDOW",
                 quota->window_minutes);
    }

    char five_remaining[16];
    char five_limit[16];
    char weekly_remaining[16];
    char weekly_limit[16];
    format_quota_number(quota->five_hour_remaining, five_remaining,
                        sizeof(five_remaining));
    format_limit(quota->five_hour_limit, five_limit, sizeof(five_limit));
    format_quota_number(quota->weekly_remaining, weekly_remaining,
                        sizeof(weekly_remaining));
    format_limit(quota->weekly_limit, weekly_limit, sizeof(weekly_limit));

    draw_header("TAKO QUOTA");
    draw_quota_row(40, window_label, five_remaining, five_limit,
                   quota->five_hour_used_pct);
    draw_quota_row(110, "WEEKLY", weekly_remaining, weekly_limit,
                   quota->weekly_used_pct);

    epd_paint_fill_rect(s_frame, 0, EPD_HEIGHT - 18, EPD_WIDTH, EPD_HEIGHT,
                        EPD_BLACK);
    epd_paint_text(s_frame, 8, EPD_HEIGHT - 13, "UPDATED", EPD_WHITE);
    epd_paint_text(s_frame,
                   right_x(EPD_WIDTH - 8, quota->fetched_at, 1),
                   EPD_HEIGHT - 13, quota->fetched_at, EPD_WHITE);
    return display_frame();
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

static bool boot_button_pressed(void)
{
    rtc_gpio_deinit(BOARD_PIN_BOOT);
    gpio_config_t input = {
        .pin_bit_mask = 1ULL << BOARD_PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&input);
    vTaskDelay(pdMS_TO_TICKS(20));
    return gpio_get_level(BOARD_PIN_BOOT) == 0;
}

static bool configuration_requested(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 ||
           boot_button_pressed();
}

static void enter_deep_sleep(uint32_t refresh_minutes)
{
    uint64_t sleep_us = (uint64_t)refresh_minutes * 60ULL * 1000000ULL;
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_us));

    rtc_gpio_init(BOARD_PIN_BOOT);
    rtc_gpio_set_direction(BOARD_PIN_BOOT, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(BOARD_PIN_BOOT);
    rtc_gpio_pulldown_dis(BOARD_PIN_BOOT);
    if (rtc_gpio_get_level(BOARD_PIN_BOOT) != 0) {
        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(
            1ULL << BOARD_PIN_BOOT, ESP_EXT1_WAKEUP_ANY_LOW));
    }

    ESP_LOGI(TAG, "deep sleep for %" PRIu32 " minutes", refresh_minutes);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    setenv("TZ", "CST-8", 1);
    tzset();

    device_config_t config;
    ESP_ERROR_CHECK(device_config_load(&config));

    bool had_valid_config = device_config_is_valid(&config);
    bool setup = !had_valid_config || configuration_requested();
    if (setup) {
        ESP_LOGI(TAG, "entering BLE configuration mode");
        log_display_result("setup", show_setup_screen());
        esp_err_t err = ble_config_run(&config, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BLE configuration stopped: %s", esp_err_to_name(err));
            enter_deep_sleep(config.refresh_minutes);
        }
    }

    esp_err_t err = network_connect(&config);
    if (err == ESP_OK) {
        err = network_sync_time();
    }

    tako_quota_t quota;
    if (err == ESP_OK) {
        err = tako_fetch_quota(&config, &quota);
    }
    network_stop();

    if (err == ESP_OK) {
        log_display_result("quota", show_quota_screen(&quota));
    } else if (!had_valid_config || setup) {
        const char *reason = err == ESP_ERR_TIMEOUT ? "WIFI FAILED" : "FETCH FAILED";
        log_display_result("error", show_error_screen(reason));
    } else {
        ESP_LOGW(TAG, "refresh failed; preserving the previous panel image");
    }

    enter_deep_sleep(config.refresh_minutes);
}
