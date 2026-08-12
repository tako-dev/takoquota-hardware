#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * 1bpp framebuffer for the 200x200 mono e-paper panel.
 *
 * Bit layout matches what the panel controller expects over SPI: each byte
 * holds 8 horizontally-adjacent pixels, MSB = leftmost. A set bit is white,
 * a clear bit is black, so a "blank" (all-white) buffer is 0xFF.
 */

#define EPD_WIDTH   200
#define EPD_HEIGHT  200

#define EPD_ROW_BYTES  (EPD_WIDTH / 8)
#define EPD_BUF_SIZE   (EPD_ROW_BYTES * EPD_HEIGHT)

typedef enum {
    EPD_WHITE = 0,
    EPD_BLACK = 1,
} epd_color_t;

/* Fill the whole buffer with one colour. */
void epd_paint_clear(uint8_t *buf, epd_color_t color);

/* Set a single pixel. Out-of-range coordinates are ignored. */
void epd_paint_pixel(uint8_t *buf, int x, int y, epd_color_t color);

/* Filled rectangle, inclusive of x0,y0 and exclusive of x1,y1. */
void epd_paint_fill_rect(uint8_t *buf, int x0, int y0, int x1, int y1, epd_color_t color);

/* One-pixel-wide rectangle outline. */
void epd_paint_rect(uint8_t *buf, int x0, int y0, int x1, int y1, epd_color_t color);

/* Horizontal and vertical lines. */
void epd_paint_hline(uint8_t *buf, int x, int y, int len, epd_color_t color);
void epd_paint_vline(uint8_t *buf, int x, int y, int len, epd_color_t color);

/*
 * Draw an ASCII string using the built-in 5x7-in-8x8 font, top-left anchored
 * at x,y. Characters outside the table render as blanks. Returns the x
 * coordinate just past the last glyph drawn.
 */
int epd_paint_text(uint8_t *buf, int x, int y, const char *s, epd_color_t color);

/* Same as epd_paint_text but each glyph is doubled in both directions. */
int epd_paint_text_2x(uint8_t *buf, int x, int y, const char *s, epd_color_t color);

/*
 * Integer-scaled text: each source pixel becomes a scale x scale block, so a
 * glyph occupies 8*scale pixels in both directions. scale <= 0 is treated as 1.
 */
int epd_paint_text_scaled(uint8_t *buf, int x, int y, const char *s,
                          epd_color_t color, int scale);
