#pragma once

#include "esp_err.h"

#include "epd_paint.h"

/*
 * Driver for the 1.54" 200x200 mono e-paper panel on the
 * Waveshare ESP32-S3-Touch-ePaper-1.54 board (SSD1681-class controller).
 *
 * Pin assignments live in board.h.
 */

/* Bring up SPI + control GPIOs and run the panel power-on init sequence. */
esp_err_t epd_init(void);

/*
 * Push a full framebuffer and trigger a full refresh. Blocks until the panel
 * reports idle, which takes on the order of a second for a full update.
 */
esp_err_t epd_display(const uint8_t *buf);

/* Drive the whole panel white without needing a caller-supplied buffer. */
esp_err_t epd_clear(void);

/*
 * Put the panel into deep sleep. The controller draws very little in this
 * state, but epd_init() must be called again before the next update.
 */
esp_err_t epd_sleep(void);

/* Cut the panel power rail, including after a failed init or refresh. */
void epd_power_off(void);
