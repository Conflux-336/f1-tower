#!/usr/bin/env python3
"""Mock the landscape UI at 320x170 with the exact firmware geometry."""
import json
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
OFFICIAL = ("/Users/aespa/.qoder/tmp/-Users-aespa-Documents-Qoder-2026-11-08-"
            "9960d41b/images/9960d41b-4f8f-4bd5-b04b-9dae63d38dd5/"
            "2908c615-5178-493e-9857-aa930d9c7311.png")

W, H = 320, 170
BG = (13, 13, 13)
PANEL = (22, 22, 22)
TEAL = (0, 215, 182)
PETRONAS = (0, 161, 155)
SILVER = (200, 200, 200)
DIM = (110, 110, 110)
WHITE = (255, 255, 255)
RED = (225, 6, 0)
YELLOW = (255, 215, 0)
ROW_ALT = (17, 17, 17)
TARGET = (7, 28, 25)
SOFT = (255, 51, 51)
MEDIUM = (255, 215, 0)
INTER = (57, 181, 74)
WET = (0, 160, 222)

FONTS = {s: ImageFont.truetype(FONT, s) for s in (10, 12, 16, 22, 28)}
POLYS = json.load(open(os.path.join(HERE, "amgmark_polys.json")))
STAR = Image.open(OFFICIAL).convert("L").crop((172, 72, 373, 273))


def tw(px, s):
    return FONTS[px].getlength(s)


def text(d, s, x, base, px, col, anchor="ls"):
    d.text((x, base), s, font=FONTS[px], fill=col, anchor=anchor)


def text_right(d, s, rx, base, px, col):
    text(d, s, rx, base, px, col, anchor="rs")


def text_center(d, s, cx, base, px, col):
    text(d, s, cx, base, px, col, anchor="ms")


def wrap_two(s, px, maxw, cap=63):
    n = min(len(s), cap)
    s = s[:n]
    cut = 0
    for i in range(1, n + 1):
        if tw(px, s[:i]) <= maxw:
            cut = i
        else:
            break
    brk = cut
    if cut < n:
        for i in range(cut, 0, -1):
            if s[i - 1] == " ":
                brk = i - 1
                break
    if brk == 0:
        brk = cut
    return s[:brk], s[brk:].lstrip(" ")


def star(dst, x, y, size):
    im = STAR.resize((size, size), Image.LANCZOS)
    dst.paste(WHITE, (x, y), im)


def amg_mark(dst, x, y, width, col=WHITE):
    s = width / 335.0
    layer = Image.new("L", (width, max(1, int(round(34 * s)))), 0)
    d = ImageDraw.Draw(layer)
    for poly in POLYS:
        d.polygon([(px * s, py * s) for px, py in poly], fill=255)
    dst.paste(col, (x, y), layer)


SEG = [0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
       0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111]


def seg7(d, x, y, h, s, on, off):
    w = h * 55 // 100
    t = 4 if h >= 30 else 3
    gap = 6 if h >= 30 else 4

    def r(px, py, pw, ph, bit):
        d.rectangle([px, py, px + pw - 1, py + ph - 1], fill=on if bit else off)

    pen = x
    for ch in s:
        if ch == ":":
            d.rectangle([pen, y + h // 2 - t - 1, pen + t - 1, y + h // 2 - 2], fill=on)
            d.rectangle([pen, y + h // 2 + 1, pen + t - 1, y + h // 2 + t], fill=on)
            pen += t + gap
        elif ch == " ":
            pen += gap * 2
        else:
            m = SEG[int(ch)]
            half = h // 2
            r(pen + t, y, w - 2 * t, t, m & 0x01)
            r(pen + t, y + half - t // 2, w - 2 * t, t, m & 0x40)
            r(pen + t, y + h - t, w - 2 * t, t, m & 0x08)
            vh = half - t - t // 2
            r(pen, y + t, t, vh, m & 0x20)
            r(pen + w - t, y + t, t, vh, m & 0x02)
            r(pen, y + half + t // 2, t, vh, m & 0x10)
            r(pen + w - t, y + half + t // 2, t, vh, m & 0x04)
            pen += w + gap


def logo(dst, x, top, width):
    s = width / 335.0
    star(dst, int(x + width * 0.5 - 100.5 * s), int(top + 100 * s), int(201 * s))
    amg_mark(dst, x, int(top + 241 * s), width)

    m = Image.new("L", (300, 46), 0)
    ImageDraw.Draw(m).text((4, 40), "PETRONAS", font=FONTS[22], fill=255,
                           anchor="ls")
    bb = m.getbbox()
    m = m.crop(bb).resize((width, bb[3] - bb[1]), Image.LANCZOS)
    dst.paste(PETRONAS, (x, int(top + 322 * s) - m.height), m)

    f1w = int(tw(12, "FORMULA 1 TEAM"))
    gap = max(0, (width - f1w) // 13)
    d = ImageDraw.Draw(dst)
    pen = x + (width - f1w - 13 * gap) // 2
    for ch in "FORMULA 1 TEAM":
        d.text((pen, top + 357 * s), ch, font=FONTS[12], fill=WHITE, anchor="ls")
        pen += int(tw(12, ch)) + gap


def screen_live():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    star(im, 4, 0, 20)
    text(d, "INTERLAGOS", 27, 15, 12, SILVER)
    text(d, "RACE", 27 + int(tw(12, "INTERLAGOS")) + 8, 14, 10, DIM)
    text_right(d, "32/71", 226, 15, 12, TEAL)
    text_right(d, "LAP", 226 - int(tw(12, "32/71")) - 4, 14, 10, DIM)
    text_right(d, "YELLOW", 316, 14, 10, YELLOW)
    d.rectangle([316 - 10 - int(tw(10, "YELLOW")), 6,
                 316 - 11 - int(tw(10, "YELLOW")) + 6, 11], fill=YELLOW)
    d.line([0, 20, W - 1, 20], fill=PETRONAS)

    rows = [("1", "VER", "VERSTAPPEN", (54, 113, 198), "M", "4L", False),
            ("2", "NOR", "NORRIS", (255, 135, 0), "M", "14L", False),
            ("3", "RUS", "RUSSELL", (0, 215, 182), "H", "4L", True),
            ("4", "ANT", "ANTONELLI", (0, 215, 182), "S", "14L", True),
            ("5", "LEC", "LECLERC", (225, 6, 0), "H", "4L", False),
            ("6", "HAM", "HAMILTON", (225, 6, 0), "M", "14L", False),
            ("7", "PIA", "PIASTRI", (255, 135, 0), "H", "4L", False),
            ("8", "SAI", "SAINZ", (0, 130, 250), "S", "14L", False)]
    lh = Image.new("RGB", (176, 16), BG)
    for i, (pos, code, last, col, comp, age, alt) in enumerate(rows):
        y = 24 + i * 16
        target = code in ("RUS", "ANT")
        bg = TARGET if target else (ROW_ALT if alt else BG)
        rd = ImageDraw.Draw(lh)
        lh.paste(bg, (0, 0, 176, 16))
        rd.rectangle([19, 3, 21, 12], fill=TEAL if target else col)
        text(rd, pos, 16, 12, 12, TEAL if pos == "1" else (WHITE if target else SILVER),
             anchor="rs")
        text(rd, code, 25, 12, 16, WHITE if target else SILVER)
        text(rd, last, 63, 12, 10, DIM)
        text(rd, comp, 136, 12, 12,
             {"S": SOFT, "M": MEDIUM, "H": WHITE, "I": INTER, "W": WET}[comp])
        text_right(rd, age, 172, 12, 10, DIM)
        im.paste(lh, (0, y))

    d = ImageDraw.Draw(im)
    x, w = 182, 134
    d.line([176, 24, 176, 151], fill=PANEL)
    text(d, "CONDITIONS", x, 34, 10, DIM)
    text(d, "TRK 41°   AIR 26°", x, 51, 12, SILVER)
    text(d, "HUM 62%   DRY", x, 65, 12, DIM)
    d.line([x, 71, x + w - 1, 71], fill=PANEL)
    text(d, "RACE CONTROL", x, 82, 10, DIM)
    a, b = wrap_two("YELLOW IN TRACK SECTOR 1", 10, w)
    text(d, a, x, 96, 10, YELLOW)
    if b:
        text(d, b, x, 108, 10, YELLOW)
    d.line([x, 118, x + w - 1, 118], fill=PANEL)
    text(d, "TEAM RADIO", x, 128, 10, DIM)
    d.rectangle([x, 136, x + 2, 148], fill=TEAL)
    text(d, "RUS", x + 7, 149, 16, WHITE)
    text_right(d, "ON AIR", x + w, 148, 10, TEAL)

    d.rectangle([0, 152, W - 1, 154], fill=PANEL)
    d.rectangle([0, 152, int(32 * W / 71), 154], fill=TEAL)
    text(d, "DEMO", 6, 166, 10, YELLOW)
    text_right(d, "39 LAPS TO GO", 314, 166, 10, SILVER)
    return im


def screen_countdown():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    star(im, 4, 0, 20)
    d = ImageDraw.Draw(im)
    text(d, "NEXT ROUND", 27, 15, 12, SILVER)
    text_right(d, "20:41", 316, 15, 12, DIM)
    d.line([0, 20, W - 1, 20], fill=PETRONAS)

    text(d, "LIGHTS OUT IN", 12, 40, 10, DIM)
    seg7(d, 12, 48, 52, "14", TEAL, (16, 34, 32))
    text(d, "DAYS", 84, 82, 12, DIM)
    seg7(d, 12, 108, 26, "12:34:05", TEAL, (16, 34, 32))

    x = 150
    text(d, "ROUND 21", x, 40, 10, DIM)
    text(d, "SAO PAULO GRAND PRIX", x, 60, 12, SILVER)
    text(d, "NOV 09  01:00", x, 98, 12, TEAL)
    text(d, "LOCAL TIME", x, 112, 10, DIM)
    d.line([x, 122, W - 5, 122], fill=PANEL)
    text(d, "MERCEDES-AMG PETRONAS", x, 140, 10, DIM)
    text(d, "DEMO", 6, 162, 10, YELLOW)
    return im


def screen_last():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    star(im, 4, 0, 20)
    d = ImageDraw.Draw(im)
    text(d, "LAST RACE", 27, 15, 12, SILVER)
    text_right(d, "MEXICO CITY GRAND PRIX", 316, 15, 12, DIM)
    d.line([0, 20, W - 1, 20], fill=PETRONAS)

    rows = [("1", "VER", "RED BULL RACING", False),
            ("2", "HAM", "FERRARI", False),
            ("3", "ANT", "MERCEDES", True),
            ("4", "LEC", "FERRARI", False),
            ("5", "NOR", "MCLAREN", False)]
    for i, (pos, code, team, target) in enumerate(rows):
        y = 28 + i * 22
        bg = TARGET if target else BG
        if target:
            d.rectangle([0, y - 2, 195, y + 19], fill=bg)
        text(d, pos, 10, y + 16, 16, TEAL if i == 0 else SILVER)
        text(d, code, 40, y + 16, 16, WHITE if target else SILVER)
        text(d, team, 92, y + 15, 10, TEAL if target else DIM)
    d.line([200, 28, 200, 135], fill=PANEL)

    x = 208
    text(d, "NEXT IN", x, 40, 10, DIM)
    text(d, "14D 12H", x, 68, 22, TEAL)
    text(d, "LAS VEGAS", x, 86, 10, SILVER)
    text(d, "GRAND PRIX", x, 98, 10, SILVER)
    text(d, "NOV 22  04:00", x, 120, 10, DIM)
    d.rectangle([0, 166, W - 1, 169], fill=PETRONAS)
    text(d, "MERCEDES-AMG PETRONAS", 6, 162, 10, DIM)
    text_right(d, "DEMO", 314, 162, 10, YELLOW)
    return im


def screen_splash():
    im = Image.new("RGB", (W, H), BG)
    logo(im, 14, 10, 140)
    d = ImageDraw.Draw(im)
    text(d, "LIVE TIMING", 176, 66, 22, SILVER)
    d.rectangle([176, 78, 235, 79], fill=PETRONAS)
    text(d, "MERCEDES-AMG PETRONAS", 176, 100, 10, DIM)
    d.rectangle([0, 166, W - 1, 169], fill=PETRONAS)
    return im


screens = [("live", screen_live()), ("countdown", screen_countdown()),
           ("last race", screen_last()), ("splash", screen_splash())]
scale = 2
sheet = Image.new("RGB",
                  (W * scale + 16, (H * scale + 20) * len(screens) + 8),
                  (40, 40, 40))
y = 0
for name, im in screens:
    big = im.resize((W * scale, H * scale), Image.NEAREST)
    sheet.paste(big, (8, y + 8))
    y += H * scale + 20
sheet.save(os.path.join(HERE, "ui_preview.png"))
print("wrote ui_preview.png", sheet.size)
