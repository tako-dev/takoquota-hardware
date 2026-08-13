#include "battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "battery";

/* GPIO4 = ADC1 channel 3. The board divides the battery by 2 before the pin. */
#define BATTERY_ADC_UNIT    ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#define BATTERY_ATTEN       ADC_ATTEN_DB_12
#define BATTERY_BITWIDTH    ADC_BITWIDTH_12
#define BATTERY_DIVIDER     2
#define BATTERY_SAMPLES     8
#define BATTERY_NO_CELL_MV  2900    /* below this there is no battery fitted */

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

/* Rough LiPo discharge curve: {millivolts, percent}. Linear between points. */
static const struct {
    int mv;
    int pct;
} s_curve[] = {
    { 4200, 100 }, { 4000, 85 }, { 3800, 65 }, { 3600, 40 },
    { 3500, 20 },  { 3300, 5 },  { 3000, 0 },
};

static esp_err_t battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "adc unit");

    adc_oneshot_chan_cfg_t chan = {
        .atten = BATTERY_ATTEN,
        .bitwidth = BATTERY_BITWIDTH,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan),
                        TAG, "adc channel");

    adc_cali_curve_fitting_config_t cali = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ATTEN,
        .bitwidth = BATTERY_BITWIDTH,
    };
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cali, &s_cali) == ESP_OK;
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "ADC curve-fitting unavailable; using raw 3.3V scale");
    }
    return ESP_OK;
}

static int raw_to_mv(int raw)
{
    if (s_cali_ok) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }
    return raw * 3300 / 4096;
}

static int mv_to_percent(int mv)
{
    int n = sizeof(s_curve) / sizeof(s_curve[0]);
    if (mv >= s_curve[0].mv) {
        return 100;
    }
    if (mv <= s_curve[n - 1].mv) {
        return 0;
    }
    for (int i = 0; i < n - 1; i++) {
        int hi_mv = s_curve[i].mv;
        int lo_mv = s_curve[i + 1].mv;
        if (mv <= hi_mv && mv >= lo_mv) {
            int hi_pct = s_curve[i].pct;
            int lo_pct = s_curve[i + 1].pct;
            return lo_pct + (mv - lo_mv) * (hi_pct - lo_pct) / (hi_mv - lo_mv);
        }
    }
    return 0;
}

int battery_read_percent(void)
{
    if (s_adc == NULL && battery_init() != ESP_OK) {
        return -1;
    }

    int total = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            return -1;
        }
        total += raw_to_mv(raw);
    }
    int mv = total * BATTERY_DIVIDER / BATTERY_SAMPLES;

    if (mv < BATTERY_NO_CELL_MV) {
        ESP_LOGI(TAG, "no battery detected (%d mV)", mv);
        return -1;  /* no battery fitted (USB powered) */
    }
    int pct = mv_to_percent(mv);
    ESP_LOGI(TAG, "battery %d mV -> %d%%", mv, pct);
    return pct;
}
