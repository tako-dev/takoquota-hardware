#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/*
 * Pin map for the Waveshare ESP32-S3-Touch-ePaper-1.54 (V2, ESP32-S3-PICO-1-N8R8).
 *
 * Taken from the official schematic, which states these twice — once in the
 * module's IO summary table and again at the J10 panel connector. Both copies
 * agree.
 *   https://files.waveshare.com/wiki/ESP32-S3-ePaper-1.54/ESP32-S3-Touch-ePaper-1.54-Schematic.pdf
 *
 * Note V1 of this board (ESP32-S3FH4R2) uses a different pin map; Waveshare
 * warns their examples are not interchangeable between versions.
 */

/* e-paper panel: 4-wire SPI, write-only (no MISO routed). */
#define EPD_PIN_BUSY    GPIO_NUM_8
#define EPD_PIN_RST     GPIO_NUM_9
#define EPD_PIN_DC      GPIO_NUM_10
#define EPD_PIN_CS      GPIO_NUM_11
#define EPD_PIN_SCLK    GPIO_NUM_12
#define EPD_PIN_MOSI    GPIO_NUM_13

/* Hold the on-board BOOT button while resetting to reopen BLE setup. */
#define BOARD_PIN_BOOT  GPIO_NUM_0

/*
 * GPIO6 carries the EPD3V3_EN net (Q2, an AO3401 P-channel MOSFET, gate pulled
 * up by R71 10K).
 *
 * The V2 board support package configures this pin as an output and drives it
 * low before initialising the panel. The name is misleading: Q2 is a P-channel
 * high-side switch with a pulled-up gate, so the enable is active low.
 */
#define EPD_PIN_PWR_EN  GPIO_NUM_6

/* Capacitive touch panel shares the RTC I2C bus (GPIO47/48). */
#define EPD_TP_PIN_RST  GPIO_NUM_7
#define EPD_TP_PIN_INT  GPIO_NUM_21

/* On-board I2C bus: PCF85063 RTC, SHTC3 sensor, and the touch controller. */
#define BOARD_I2C_SDA   GPIO_NUM_47
#define BOARD_I2C_SCL   GPIO_NUM_48

#define EPD_SPI_HOST    SPI2_HOST

/*
 * SSD1681-class controllers are rated to 20MHz. 10MHz keeps plenty of margin
 * over the flat-flex connector while still pushing a 5KB frame in a few ms.
 */
#define EPD_SPI_CLOCK_HZ  (10 * 1000 * 1000)

/*
 * Battery voltage is read on ADC1 channel 3 (GPIO4) through the on-board 1/2
 * divider. The channel and divider constants live in battery.c.
 */
