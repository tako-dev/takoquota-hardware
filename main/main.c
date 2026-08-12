#include <stdio.h>
#include <string.h>

#include "board.h"
#include "epd_1in54.h"
#include "epd_paint.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/* One 200x200 mono frame is 5000 bytes — fine as a static allocation. */
static uint8_t s_frame[EPD_BUF_SIZE];

/*
 * One-shot snapshot of `tako quota` (2026-08-12 12:21 +08). No fetching on
 * device — these are the numbers baked into the image:
 *   5h window: 15% used, 85.0 remaining of 100
 *   weekly:    27% used, 674.5 remaining of 930
 */
#define QUOTA_5H_USED_PCT   15
#define QUOTA_5H_REMAIN     "85.0"
#define QUOTA_WK_USED_PCT   27
#define QUOTA_WK_REMAIN     "674.5"
#define QUOTA_STAMP         "2026-08-12 12:21"

/* Right-align helper: x where a scaled string of length n ends at x_right. */
static int right_x(int x_right, const char *s, int scale)
{
    return x_right - (int)strlen(s) * 8 * scale;
}

/*
 * One quota row: label, big remaining number, and a thick solid bar.
 * Everything is a solid block — no 1px textures, which just read as grey haze
 * on a panel this small.
 */
static void draw_quota_row(uint8_t *buf, int y0, const char *label,
                           const char *number, const char *of, int used_pct)
{
    const int x0 = 8, x1 = EPD_WIDTH - 8;

    /* Label, small, with a short rule under it spanning the label only. */
    epd_paint_text(buf, x0, y0, label, EPD_BLACK);

    /* "USED nn%" right-aligned on the label line. */
    char pct[12];
    snprintf(pct, sizeof(pct), "USED %d%%", used_pct);
    epd_paint_text(buf, right_x(x1, pct, 1), y0, pct, EPD_BLACK);

    /* Big remaining figure at 3x (24px tall) — the thing you read at a glance. */
    int nx = epd_paint_text_scaled(buf, x0, y0 + 13, number, EPD_BLACK, 3);

    /* "/ 930" trailing at 1x, baseline-aligned to the bottom of the big digits. */
    epd_paint_text(buf, nx + 4, y0 + 13 + 16, of, EPD_BLACK);

    /*
     * Bar: 14px tall so it survives the panel's contrast. Outer frame is 2px,
     * fill shows what is left rather than what is spent.
     */
    const int bar_y = y0 + 44, bar_h = 14;
    epd_paint_fill_rect(buf, x0, bar_y, x1, bar_y + bar_h, EPD_BLACK);
    epd_paint_fill_rect(buf, x0 + 2, bar_y + 2, x1 - 2, bar_y + bar_h - 2, EPD_WHITE);
    int inner_w = (x1 - 2) - (x0 + 2);
    int fill_w = inner_w * (100 - used_pct) / 100;
    epd_paint_fill_rect(buf, x0 + 2, bar_y + 2, x0 + 2 + fill_w, bar_y + bar_h - 2,
                        EPD_BLACK);
}

/* Solid-block quota readout: high contrast, no fine textures. */
static void draw_quota_screen(void)
{
    epd_paint_clear(s_frame, EPD_WHITE);

    /* Title: reversed-out text in a solid band across the top. */
    epd_paint_fill_rect(s_frame, 0, 0, EPD_WIDTH, 30, EPD_BLACK);
    epd_paint_text_2x(s_frame, 8, 7, "TAKO QUOTA", EPD_WHITE);

    draw_quota_row(s_frame, 40, "5H WINDOW", QUOTA_5H_REMAIN, "/ 100",
                   QUOTA_5H_USED_PCT);
    draw_quota_row(s_frame, 110, "WEEKLY", QUOTA_WK_REMAIN, "/ 930",
                   QUOTA_WK_USED_PCT);

    /* Footer: solid band, reversed-out timestamp. */
    epd_paint_fill_rect(s_frame, 0, EPD_HEIGHT - 18, EPD_WIDTH, EPD_HEIGHT, EPD_BLACK);
    epd_paint_text(s_frame, 8, EPD_HEIGHT - 13, "SNAPSHOT", EPD_WHITE);
    epd_paint_text(s_frame, right_x(EPD_WIDTH - 8, QUOTA_STAMP, 1),
                   EPD_HEIGHT - 13, QUOTA_STAMP, EPD_WHITE);
}

void app_main(void)
{
    ESP_LOGI(TAG, "e-paper quota screen starting");

    ESP_ERROR_CHECK(epd_init());

    /*
     * Blank first. An e-paper panel holds whatever was last written to it, so
     * without this any leftover image would show through the new one as ghosting.
     */
    ESP_LOGI(TAG, "clearing panel");
    ESP_ERROR_CHECK(epd_clear());

    ESP_LOGI(TAG, "drawing quota screen");
    draw_quota_screen();
    ESP_ERROR_CHECK(epd_display(s_frame));

    ESP_LOGI(TAG, "done, putting panel to sleep (image stays on screen)");
    ESP_ERROR_CHECK(epd_sleep());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
