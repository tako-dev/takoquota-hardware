# epaper-154

Lights up the 1.54" 200x200 e-paper display on a **Waveshare ESP32-S3-Touch-ePaper-1.54 (V2)**.
Draws a quota snapshot, then puts the panel to sleep — the image stays on screen without power.

Built and verified against ESP-IDF **v5.2.3** on real hardware.

## Pin map

From the [official schematic](https://files.waveshare.com/wiki/ESP32-S3-ePaper-1.54/ESP32-S3-Touch-ePaper-1.54-Schematic.pdf),
which lists these twice (module IO table + J10 connector); both copies agree.

| Signal | GPIO |
|---|---|
| EPD_BUSY | 8 |
| EPD_RST | 9 |
| EPD_D/C | 10 |
| EPD_CS | 11 |
| EPD_SCLK | 12 |
| EPD_SDI (MOSI) | 13 |
| EPD3V3_EN | 6 (active low) |
| EPD_TP_RST / EPD_TP_INT | 7 / 21 |
| Board I2C SDA / SCL | 47 / 48 |

Two things worth knowing:

- **GPIO6 (`EPD3V3_EN`) is active low.** The driver configures it as an output
  and drives it low before resetting the panel, matching Waveshare's V2 BSP.
- **V1 boards (ESP32-S3FH4R2) use a different pin map.** Waveshare states their
  examples are not interchangeable between versions. This project targets V2
  (ESP32-S3-PICO-1-N8R8, 8MB flash / 8MB PSRAM).

## Build and flash

```bash
# activate ESP-IDF first: C:\Users\46907\esp\v5.2.3\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py -p COM19 flash monitor
```

Note the export script rejects MSys/MinGW shells, so run it from PowerShell.

## Layout

| File | Purpose |
|---|---|
| `main/board.h` | pin assignments and SPI config |
| `main/epd_1in54.c` | SSD1681 init / refresh / sleep over SPI |
| `main/epd_paint.c` | 1bpp framebuffer: pixels, rects, lines, text |
| `main/font8x8.c` | 5x7 ASCII glyphs in 8x8 cells |
| `main/main.c` | quota screen and demo flow |

## PSRAM

Disabled in `sdkconfig.defaults`. The module reports 8MB, but enabling quad
PSRAM fails ID detection (`PSRAM ID read error: 0x00ffffff`) and reboot-loops.
The framebuffer only needs 5KB of internal RAM, so this is left alone rather
than chased down. If a later feature needs PSRAM, the octal/quad line mode and
speed settings are the place to start.
