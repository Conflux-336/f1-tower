#!/usr/bin/env python3
"""Compose the README hero banner from the real UI render.

Uses the same three-pointed-star geometry as preview_logo.py and a 2x
NEAREST upscale of tools/screen_live.png, so the banner matches the panel.
"""
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

from preview_logo import in_star

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

W, H = 1600, 480
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

BG_TOP = (9, 9, 9)
BG_BOT = (17, 21, 20)
WHITE = (245, 245, 245)
SILVER = (196, 196, 196)
DIM = (118, 118, 118)
TEAL = (0, 161, 155)


def star_mask(size, ss=4):
    mask = Image.new("L", (size * ss, size * ss), 0)
    px = mask.load()
    c = size * ss / 2.0
    r = c * 0.985
    for y in range(size * ss):
        dy = y + 0.5 - c
        for x in range(size * ss):
            if in_star(x + 0.5 - c, dy, r):
                px[x, y] = 255
    return mask.resize((size, size), Image.LANCZOS)


def tracked(draw, xy, text, font, fill, spacing):
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill, anchor="ls")
        x += font.getlength(ch) + spacing
    return x


def main():
    img = Image.new("RGB", (W, H), BG_TOP)
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        row = tuple(round(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3))
        for x in range(W):
            px[x, y] = row

    # soft teal glow behind the screen
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse([760, -60, 1560, 560], fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(90))
    img.paste(Image.new("RGB", (W, H), TEAL), (0, 0), glow.point(lambda v: v // 6))

    # oversized star watermark, barely visible
    mark = Image.open(os.path.join(HERE, "star_120.png")).convert("L")
    mark = mark.resize((380, 380), Image.LANCZOS)
    alpha = Image.new("L", (W, H), 0)
    alpha.paste(mark.point(lambda v: v // 14), (140, 45))
    img = Image.composite(Image.new("RGB", (W, H), WHITE), img, alpha)

    d = ImageDraw.Draw(img)

    # small solid star + kicker
    s = 30
    sm = star_mask(s, ss=4)
    img.paste(Image.new("RGB", (s, s), TEAL), (82, 108), sm)
    f_kick = ImageFont.truetype(FONT, 21)
    tracked(d, (82 + s + 14, 108 + s - 5), "MERCEDES-AMG PETRONAS", f_kick, TEAL, 4.2)

    # title: F1 white, TOWER teal
    f_title = ImageFont.truetype(FONT, 126)
    x = 78
    y = 300
    d.text((x, y), "F1", font=f_title, fill=WHITE, anchor="ls")
    x += f_title.getlength("F1") + 22
    d.text((x, y), "TOWER", font=f_title, fill=TEAL, anchor="ls")

    # teal rule
    d.rectangle([82, 322, 146, 326], fill=TEAL)

    f_sub = ImageFont.truetype(FONT, 31)
    d.text((82, 372), "LIVE TIMING FOR ESP32-S3", font=f_sub, fill=SILVER, anchor="ls")
    f_meta = ImageFont.truetype(FONT, 22)
    d.text((82, 414), "OpenF1  ·  Jolpica  ·  ST7789 170×320  ·  WiFi", font=f_meta,
           fill=DIM, anchor="ls")

    # panel screenshot, 2x nearest to keep the pixel grid sharp
    shot = Image.open(os.path.join(HERE, "screen_live.png")).convert("RGB")
    shot = shot.resize((shot.width * 2, shot.height * 2), Image.NEAREST)
    sx, sy = 880, 70
    frame = (sx - 12, sy - 12, sx + shot.width + 12, sy + shot.height + 12)

    shadow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(shadow).rounded_rectangle(frame, radius=16, fill=170)
    shadow = shadow.filter(ImageFilter.GaussianBlur(26))
    img.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), shadow.point(lambda v: v // 2))

    d.rounded_rectangle(frame, radius=16, fill=(18, 18, 18), outline=(38, 74, 72), width=2)
    img.paste(shot, (sx, sy))

    # PETRONAS bar along the bottom edge, as on the panel footer
    d.rectangle([0, H - 6, W - 1, H - 1], fill=TEAL)

    out = os.path.join(ROOT, "docs", "banner.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out, optimize=True)
    print("wrote", out, img.size)


if __name__ == "__main__":
    main()
