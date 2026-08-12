"""Host-side render of draw_quota_screen() to verify layout before flashing.

Mirrors the epd_paint primitives 1:1 and parses font8x8.c for glyph data.
"""
import re
import struct
import sys
import zlib

W = H = 200
ROW_BYTES = W // 8
BLACK, WHITE = 1, 0

buf = bytearray([0xFF] * (ROW_BYTES * H))


def pixel(x, y, color):
    if not (0 <= x < W and 0 <= y < H):
        return
    i = y * ROW_BYTES + x // 8
    mask = 0x80 >> (x % 8)
    if color == BLACK:
        buf[i] &= ~mask & 0xFF
    else:
        buf[i] |= mask


def fill_rect(x0, y0, x1, y1, color):
    for y in range(y0, y1):
        for x in range(x0, x1):
            pixel(x, y, color)


# Parse font8x8.c (glyph rows are 0bXXXXXXXX binary or plain decimal 0).
src = open(r"E:\smart\project\epaper-154\main\font8x8.c", encoding="utf-8").read()
rows = re.findall(r"\{\s*((?:0b[01]{8}|\d+)\s*(?:,\s*(?:0b[01]{8}|\d+)\s*){7})\}", src)
FONT = {}
for i, bits in enumerate(rows):
    FONT[0x20 + i] = [int(p.strip(), 2) if p.strip().startswith("0b") else int(p)
                      for p in bits.split(",")]


def text(x, y, s, color, scale=1):
    for ch in s:
        glyph = FONT.get(ord(ch), [0] * 8)
        for row in range(8):
            bits = glyph[row]
            for col in range(8):
                if bits & (0x80 >> col):
                    for dy in range(scale):
                        for dx in range(scale):
                            pixel(x + col * scale + dx, y + row * scale + dy, color)
        x += 8 * scale
    return x


def right_x(x_right, s, scale=1):
    return x_right - len(s) * 8 * scale


def quota_row(y0, label, number, of, used_pct):
    x0, x1 = 8, W - 8
    text(x0, y0, label, BLACK)
    pct = f"USED {used_pct}%"
    text(right_x(x1, pct), y0, pct, BLACK)
    nx = text(x0, y0 + 13, number, BLACK, scale=3)
    text(nx + 4, y0 + 13 + 16, of, BLACK)
    bar_y, bar_h = y0 + 44, 14
    fill_rect(x0, bar_y, x1, bar_y + bar_h, BLACK)
    fill_rect(x0 + 2, bar_y + 2, x1 - 2, bar_y + bar_h - 2, WHITE)
    inner_w = (x1 - 2) - (x0 + 2)
    fill_w = inner_w * (100 - used_pct) // 100
    fill_rect(x0 + 2, bar_y + 2, x0 + 2 + fill_w, bar_y + bar_h - 2, BLACK)


fill_rect(0, 0, W, 30, BLACK)
text(8, 7, "TAKO QUOTA", WHITE, scale=2)

quota_row(40, "5H WINDOW", "85.0", "/ 100", 15)
quota_row(110, "WEEKLY", "674.5", "/ 930", 27)

stamp = "2026-08-12 12:21"
fill_rect(0, H - 18, W, H, BLACK)
text(8, H - 13, "SNAPSHOT", WHITE)
text(right_x(W - 8, stamp), H - 13, stamp, WHITE)

# Emit a 2x-scaled PNG so it is easy to eyeball.
scale = 2
out = bytearray()
for y in range(H * scale):
    out.append(0)
    for x in range(W * scale):
        sx, sy = x // scale, y // scale
        black = not (buf[sy * ROW_BYTES + sx // 8] & (0x80 >> (sx % 8)))
        out.append(0 if black else 255)


def chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 0, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(out)))
png += chunk(b"IEND", b"")

path = sys.argv[1] if len(sys.argv) > 1 else "render_preview.png"
open(path, "wb").write(png)
print(f"wrote {path}")
