"""Render the firmware's 200x200 screens on the host for layout checks."""

import argparse
import re
import struct
import zlib
from pathlib import Path


WIDTH = HEIGHT = 200
ROW_BYTES = WIDTH // 8
BLACK, WHITE = 1, 0

ROOT = Path(__file__).resolve().parent
FONT_SOURCE = ROOT / "main" / "font8x8.c"


def load_font():
    source = FONT_SOURCE.read_text(encoding="utf-8")
    rows = re.findall(
        r"\{\s*((?:0b[01]{8}|\d+)\s*(?:,\s*(?:0b[01]{8}|\d+)\s*){7})\}",
        source,
    )
    return {
        0x20 + index: [
            int(value.strip(), 2)
            if value.strip().startswith("0b")
            else int(value)
            for value in bits.split(",")
        ]
        for index, bits in enumerate(rows)
    }


FONT = load_font()


class Canvas:
    def __init__(self):
        self.buffer = bytearray([0xFF] * (ROW_BYTES * HEIGHT))

    def pixel(self, x, y, color):
        if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
            return
        index = y * ROW_BYTES + x // 8
        mask = 0x80 >> (x % 8)
        if color == BLACK:
            self.buffer[index] &= ~mask & 0xFF
        else:
            self.buffer[index] |= mask

    def fill_rect(self, x0, y0, x1, y1, color):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.pixel(x, y, color)

    def hline(self, x, y, length, color):
        for offset in range(length):
            self.pixel(x + offset, y, color)

    def vline(self, x, y, length, color):
        for offset in range(length):
            self.pixel(x, y + offset, color)

    def rect(self, x0, y0, x1, y1, color):
        self.hline(x0, y0, x1 - x0, color)
        self.hline(x0, y1 - 1, x1 - x0, color)
        self.vline(x0, y0, y1 - y0, color)
        self.vline(x1 - 1, y0, y1 - y0, color)

    def text(self, x, y, value, color, scale=1):
        for character in value:
            glyph = FONT.get(ord(character), [0] * 8)
            for row, bits in enumerate(glyph):
                for column in range(8):
                    if not bits & (0x80 >> column):
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            self.pixel(
                                x + column * scale + dx,
                                y + row * scale + dy,
                                color,
                            )
            x += 8 * scale
        return x


def right_x(x_right, value, scale=1):
    return x_right - len(value) * 8 * scale


def centered(canvas, y, value, scale=1):
    x = max(0, (WIDTH - len(value) * 8 * scale) // 2)
    canvas.text(x, y, value, BLACK, scale)


def header(canvas, title):
    canvas.fill_rect(0, 0, WIDTH, 30, BLACK)
    canvas.text(8, 7, title, WHITE, 2)


def setup_screen(canvas):
    header(canvas, "BLE SETUP")
    centered(canvas, 48, "TAKO-EPAPER")
    canvas.rect(22, 72, 178, 127, BLACK)
    centered(canvas, 83, "PAIR WITH PHONE")
    centered(canvas, 103, "WAITING...")
    centered(canvas, 148, "CONFIGURE THEN SAVE")
    centered(canvas, 172, "BLE STAYS ON")


def error_screen(canvas):
    header(canvas, "TAKO ERROR")
    centered(canvas, 55, "WIFI FAILED", 2)
    canvas.hline(24, 94, 152, BLACK)
    centered(canvas, 112, "LAST UPDATE FAILED")
    centered(canvas, 145, "PRESS BOOT")
    centered(canvas, 162, "FOR BLE SETUP")


def quota_row(canvas, y0, label, number, limit, used_pct):
    x0, x1 = 8, WIDTH - 8
    canvas.text(x0, y0, label, BLACK)
    percent = f"USED {used_pct}%"
    canvas.text(right_x(x1, percent), y0, percent, BLACK)

    number_scale = 3 if len(number) <= 5 else 2
    number_end = canvas.text(x0, y0 + 13, number, BLACK, number_scale)
    baseline = y0 + 13 + number_scale * 8 - 8
    canvas.text(number_end + 4, baseline, limit, BLACK)

    bar_y, bar_height = y0 + 44, 14
    canvas.fill_rect(x0, bar_y, x1, bar_y + bar_height, BLACK)
    canvas.fill_rect(x0 + 2, bar_y + 2, x1 - 2, bar_y + bar_height - 2, WHITE)
    inner_width = (x1 - 2) - (x0 + 2)
    fill_width = inner_width * (100 - used_pct) // 100
    canvas.fill_rect(
        x0 + 2,
        bar_y + 2,
        x0 + 2 + fill_width,
        bar_y + bar_height - 2,
        BLACK,
    )


def quota_screen(canvas):
    header(canvas, "TAKO QUOTA")
    quota_row(canvas, 40, "5H WINDOW", "139.1", "/ 200", 30)
    quota_row(canvas, 110, "WEEKLY", "628.6", "/ 930", 32)
    timestamp = "08-12 18:00"
    canvas.fill_rect(0, HEIGHT - 18, WIDTH, HEIGHT, BLACK)
    canvas.text(8, HEIGHT - 13, "UPDATED", WHITE)
    canvas.text(right_x(WIDTH - 8, timestamp), HEIGHT - 13, timestamp, WHITE)


def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def write_png(canvas, path, scale=2):
    pixels = bytearray()
    for y in range(HEIGHT * scale):
        pixels.append(0)
        for x in range(WIDTH * scale):
            source_x, source_y = x // scale, y // scale
            white = canvas.buffer[
                source_y * ROW_BYTES + source_x // 8
            ] & (0x80 >> (source_x % 8))
            pixels.append(255 if white else 0)

    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", WIDTH * scale, HEIGHT * scale, 8, 0, 0, 0, 0),
    )
    png += png_chunk(b"IDAT", zlib.compress(bytes(pixels)))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screen", choices=("setup", "quota", "error"))
    parser.add_argument("output", nargs="?", type=Path, default=Path("render_preview.png"))
    args = parser.parse_args()

    canvas = Canvas()
    {
        "setup": setup_screen,
        "quota": quota_screen,
        "error": error_screen,
    }[args.screen](canvas)
    write_png(canvas, args.output)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
