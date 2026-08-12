#include "epd_paint.h"

#include <string.h>

#include "font8x8.h"

void epd_paint_clear(uint8_t *buf, epd_color_t color)
{
    memset(buf, color == EPD_BLACK ? 0x00 : 0xFF, EPD_BUF_SIZE);
}

void epd_paint_pixel(uint8_t *buf, int x, int y, epd_color_t color)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }

    uint8_t *p = &buf[y * EPD_ROW_BYTES + (x / 8)];
    uint8_t mask = 0x80 >> (x % 8);

    if (color == EPD_BLACK) {
        *p &= (uint8_t)~mask;
    } else {
        *p |= mask;
    }
}

void epd_paint_fill_rect(uint8_t *buf, int x0, int y0, int x1, int y1, epd_color_t color)
{
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            epd_paint_pixel(buf, x, y, color);
        }
    }
}

void epd_paint_hline(uint8_t *buf, int x, int y, int len, epd_color_t color)
{
    for (int i = 0; i < len; i++) {
        epd_paint_pixel(buf, x + i, y, color);
    }
}

void epd_paint_vline(uint8_t *buf, int x, int y, int len, epd_color_t color)
{
    for (int i = 0; i < len; i++) {
        epd_paint_pixel(buf, x, y + i, color);
    }
}

void epd_paint_rect(uint8_t *buf, int x0, int y0, int x1, int y1, epd_color_t color)
{
    epd_paint_hline(buf, x0, y0, x1 - x0, color);
    epd_paint_hline(buf, x0, y1 - 1, x1 - x0, color);
    epd_paint_vline(buf, x0, y0, y1 - y0, color);
    epd_paint_vline(buf, x1 - 1, y0, y1 - y0, color);
}

int epd_paint_text(uint8_t *buf, int x, int y, const char *s, epd_color_t color)
{
    for (; *s; s++) {
        const uint8_t *glyph = font8x8_glyph((unsigned char)*s);

        for (int row = 0; row < FONT8X8_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT8X8_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    epd_paint_pixel(buf, x + col, y + row, color);
                }
            }
        }
        x += FONT8X8_WIDTH;
    }
    return x;
}

int epd_paint_text_2x(uint8_t *buf, int x, int y, const char *s, epd_color_t color)
{
    return epd_paint_text_scaled(buf, x, y, s, color, 2);
}

int epd_paint_text_scaled(uint8_t *buf, int x, int y, const char *s,
                          epd_color_t color, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    for (; *s; s++) {
        const uint8_t *glyph = font8x8_glyph((unsigned char)*s);

        for (int row = 0; row < FONT8X8_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT8X8_WIDTH; col++) {
                if (!(bits & (0x80 >> col))) {
                    continue;
                }
                /* Expand each source pixel into a scale x scale block. */
                int px = x + col * scale;
                int py = y + row * scale;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        epd_paint_pixel(buf, px + dx, py + dy, color);
                    }
                }
            }
        }
        x += FONT8X8_WIDTH * scale;
    }
    return x;
}
