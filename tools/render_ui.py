#!/usr/bin/env python3
"""Pixel-exact mock of the firmware UI.

Reads the actual fonts/stars/paths from f1tower/assets.h and f1tower/amgmark.h,
and re-implements theme.cpp + ui.cpp drawing so the sheet matches the panel.
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "f1tower")

W, H = 320, 170


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def blend(fg, bg, a):
    if a <= 0:
        return bg
    if a >= 255:
        return fg
    inv = 255 - a
    r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * inv) // 255
    g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * inv) // 255
    b = ((fg & 0x1F) * a + (bg & 0x1F) * inv) // 255
    return (r << 11) | (g << 5) | b


C = {k: rgb565(*v) for k, v in dict(
    BG=(13, 13, 13), PANEL=(22, 22, 22), TEAL=(0, 215, 182),
    PETRONAS=(0, 161, 155), SILVER=(200, 200, 200), DIM=(110, 110, 110),
    WHITE=(255, 255, 255), RED=(225, 6, 0), YELLOW=(255, 215, 0),
    ROW_ALT=(17, 17, 17), TARGET=(7, 28, 25), SOFT=(255, 51, 51),
    MEDIUM=(255, 215, 0), HARD=(255, 255, 255), INTER=(57, 181, 74),
    WET=(0, 160, 222), PURPLE=(168, 40, 240), PBEST=(60, 220, 90),
    SEGOFF=(16, 34, 32)).items()}

SRC = open(os.path.join(FW, "assets.h")).read()


class Font:
    def __init__(self, size):
        m = re.search(r"static const Glyph font%d_glyphs\[\] PROGMEM = \{(.*?)\n\};"
                      % size, SRC, re.S)
        rows = re.findall(
            r"\{0x([0-9A-Fa-f]+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+),(\d+)\}",
            m.group(1))
        self.g = {}
        for cp, w, h, xo, yo, adv, off in rows:
            self.g[int(cp, 16)] = (int(w), int(h), int(xo), int(yo), int(adv),
                                   int(off))
        m = re.search(r"static const uint8_t font%d_alpha\[\] PROGMEM = \{(.*?)\n\};"
                      % size, SRC, re.S)
        self.a = [int(x) for x in re.findall(r"\d+", m.group(1))]

    def width(self, s):
        return sum(self.g[ord(c)][4] for c in s if ord(c) in self.g)


FONTS = {n: Font(n) for n in (10, 12, 16, 22, 28)}


def star_mask(size):
    m = re.search(r"static const uint8_t star%d_alpha\[\] PROGMEM = \{(.*?)\n\};"
                  % size, SRC, re.S)
    return [int(x) for x in re.findall(r"\d+", m.group(1))]


STAR20 = star_mask(20)
STAR44 = star_mask(44)


class Canvas:
    def __init__(self):
        self.im = Image.new("RGB", (W, H), from565(C["BG"]))
        self.px = self.im.load()

    def put(self, x, y, c565):
        if 0 <= x < W and 0 <= y < H:
            self.px[x, y] = from565(c565)

    def blend_px(self, x, y, fg, bg, a):
        if 0 <= x < W and 0 <= y < H and a:
            self.px[x, y] = from565(blend(fg, bg, a))

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.put(xx, yy, c)

    def hline(self, x, y, w, c):
        for xx in range(x, x + w):
            self.put(xx, y, c)

    def vline(self, x, y, h, c):
        for yy in range(y, y + h):
            self.put(x, yy, c)

    def text(self, fs, x, baseline, s, fg, bg):
        f = FONTS[fs]
        pen = x
        for ch in s:
            g = f.g.get(ord(ch))
            if not g:
                continue
            gw, gh, xo, yo, adv, off = g
            for yy in range(gh):
                for xx in range(gw):
                    a = f.a[off + yy * gw + xx]
                    if a:
                        self.blend_px(pen + xo + xx, baseline + yo + yy, fg, bg, a)
            pen += adv

    def text_right(self, fs, rx, baseline, s, fg, bg):
        self.text(fs, rx - FONTS[fs].width(s), baseline, s, fg, bg)

    def text_center(self, fs, cx, baseline, s, fg, bg):
        self.text(fs, cx - FONTS[fs].width(s) // 2, baseline, s, fg, bg)

    def text_styled(self, fs, x, baseline, s, fg, bg, xscale, tracking):
        import math
        f = FONTS[fs]
        pen = float(x)
        for ch in s:
            g = f.g.get(ord(ch))
            if not g:
                continue
            gw, gh, xo, yo, adv, off = g
            gx = pen + xo * xscale
            for row in range(gh):
                y = baseline + yo + row
                for px in range(int(math.floor(gx)), int(math.ceil(gx + gw * xscale))):
                    u0 = (px - gx) / xscale
                    u1 = (px + 1 - gx) / xscale
                    acc = 0
                    for k in range(3):
                        u = u0 + (u1 - u0) * (0.5 + k) / 3.0
                        ui = int(math.floor(u))
                        fu = u - ui
                        if 0 <= ui < gw:
                            acc += f.a[off + row * gw + ui] * (1.0 - fu)
                        if 0 <= ui + 1 < gw:
                            acc += f.a[off + row * gw + ui + 1] * fu
                    a = int(acc / 3)
                    if a:
                        self.blend_px(px, y, fg, bg, a)
            pen += int(adv * xscale + 0.5) + tracking

    def star(self, x, y, size, fg, bg, mask):
        p = 0
        for row in range(size):
            for col in range(0, size, 2):
                b = mask[p]
                p += 1
                a0 = (b >> 4) * 17
                a1 = (b & 0x0F) * 17
                if a0:
                    self.blend_px(x + col, y + row, fg, bg, a0)
                if a1 and col + 1 < size:
                    self.blend_px(x + col + 1, y + row, fg, bg, a1)


SEG = [0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
       0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111]


def seg7(cv, x, y, h, s, on, off):
    w = h * 55 // 100
    t = 4 if h >= 30 else 3
    gap = 6 if h >= 30 else 4
    pen = x
    for ch in s:
        if ch == ":":
            cv.rect(pen, y + h // 2 - t - 1, t, t, on)
            cv.rect(pen, y + h // 2 + 1, t, t, on)
            pen += t + gap
        elif ch == " ":
            pen += gap * 2
        elif ch.isdigit():
            m = SEG[int(ch)]
            half = h // 2
            vh = half - t - t // 2
            cv.rect(pen + t, y, w - 2 * t, t, on if m & 0x01 else off)
            cv.rect(pen + t, y + half - t // 2, w - 2 * t, t, on if m & 0x40 else off)
            cv.rect(pen + t, y + h - t, w - 2 * t, t, on if m & 0x08 else off)
            cv.rect(pen, y + t, t, vh, on if m & 0x20 else off)
            cv.rect(pen + w - t, y + t, t, vh, on if m & 0x02 else off)
            cv.rect(pen, y + half + t // 2, t, vh, on if m & 0x10 else off)
            cv.rect(pen + w - t, y + half + t // 2, t, vh, on if m & 0x04 else off)
            pen += w + gap


# ---- logo pieces ----------------------------------------------------------
AMG_SRC = open(os.path.join(FW, "amgmark.h")).read()
AMG_W = int(re.search(r"AMG_W = (\d+)", AMG_SRC).group(1))
AMG_H = int(re.search(r"AMG_H = (\d+)", AMG_SRC).group(1))
_pts = [int(v) for v in re.findall(r"-?\d+", re.search(
    r"AMG_PTS\[\] = \{(.*?)\};", AMG_SRC, re.S).group(1))]
_polys = [tuple(int(v) for v in re.findall(r"-?\d+", blk)) for blk in re.findall(
    r"\{([^{}]+)\}", re.search(r"AMG_POLYS\[\] = \{(.*?)\};", AMG_SRC, re.S).group(1))]
AMG_POLYS = [(_pts[p[0] * 2:p[0] * 2 + p[1] * 2]) for p in _polys]


def in_arm(px, py, r_tip, r_notch, tip_half):
    nx = r_notch * 0.8660254
    ny = r_notch * 0.5
    vx = (0, nx, tip_half, -tip_half, -nx)
    vy = (0, ny, r_tip, r_tip, ny)
    for i in range(5):
        j = (i + 1) % 5
        ex, ey = vx[j] - vx[i], vy[j] - vy[i]
        wx, wy = px - vx[i], py - vy[i]
        if ex * wy - ey * wx < 0:
            return False
    return True


def star_aa(cv, cx, cy, R, fg, bg):
    import math
    arms = []
    for i in range(3):
        a = (90.0 + 120.0 * i) * math.pi / 180.0
        arms.append((math.sin(a), math.cos(a)))
    r_in = R * 0.91
    r_notch = R * 0.166
    tip = r_in * 0.043
    SS = 3
    for y in range(int(math.floor(cy - R)) - 1, int(math.ceil(cy + R)) + 1):
        for x in range(int(math.floor(cx - R)) - 1, int(math.ceil(cx + R)) + 1):
            hit = 0
            for sy in range(SS):
                for sx in range(SS):
                    fx = x + (sx + 0.5) / SS - cx
                    fy = y + (sy + 0.5) / SS - cy
                    d2 = fx * fx + fy * fy
                    if d2 > R * R:
                        continue
                    if d2 >= r_in * r_in:
                        hit += 1
                        continue
                    for sa, ca in arms:
                        if in_arm(fx * sa + fy * ca, fx * ca - fy * sa, r_in,
                                  r_notch, tip):
                            hit += 1
                            break
            if hit:
                cv.blend_px(x, y, fg, bg, hit * 255 // (SS * SS))


def amg_mark(cv, x, y, width, fg, bg):
    s = width / float(AMG_W)
    h = int(round(AMG_H * s))
    SUB = 3
    for j in range(h):
        acc = [0] * width
        for k in range(SUB):
            ym = (j + (k + 0.5) / SUB) / s
            xs = []
            for poly in AMG_POLYS:
                n = len(poly) // 2
                for i in range(n):
                    i2 = 0 if i + 1 == n else i + 1
                    x1, y1 = poly[i * 2], poly[i * 2 + 1]
                    x2, y2 = poly[i2 * 2], poly[i2 * 2 + 1]
                    if (y1 <= ym < y2) or (y2 <= ym < y1):
                        xs.append(x1 + (ym - y1) * (x2 - x1) / (y2 - y1))
            xs.sort()
            for a in range(0, len(xs) - 1, 2):
                lo, hi = xs[a] * s, xs[a + 1] * s
                if hi <= 0 or lo >= width:
                    continue
                lo = max(lo, 0)
                hi = min(hi, width)
                for px in range(int(lo), int(-(-hi // 1))):
                    cov = min(hi, px + 1.0) - max(lo, float(px))
                    if cov > 0:
                        acc[px] += int(cov * 85.0)
        for px in range(width):
            if acc[px]:
                cv.blend_px(x + px, y + j, fg, bg, min(acc[px], 255))


def font_for_cap(cap):
    table = [(10, 7.2), (12, 8.6), (16, 11.6), (22, 16.0), (28, 20.3)]
    return min(table, key=lambda e: abs(e[1] - cap))[0]


def team_logo(cv, x, top, width, bg):
    s = width / float(AMG_W)
    star_aa(cv, x + width * 0.5, top + 100.0 * s, 100.5 * s, C["WHITE"], bg)
    amg_mark(cv, x, top + int(241 * s), width, C["WHITE"], bg)
    pf = font_for_cap(34 * s)
    pw = FONTS[pf].width("PETRONAS")
    cv.text_styled(pf, x, top + int(322 * s), "PETRONAS", C["PETRONAS"], bg,
                   width / float(pw), 0)
    ff = font_for_cap(23 * s)
    f1 = "FORMULA 1 TEAM"
    f1w = FONTS[ff].width(f1)
    gap = max(0, (width - f1w) // 13)
    cv.text_styled(ff, x + (width - f1w - 13 * gap) // 2, top + int(357 * s), f1,
                   C["WHITE"], bg, 1.0, gap)
    return int(357 * s)


def wrap_two(s, fs, maxw, cap=63):
    n = min(len(s), cap)
    s = s[:n]
    cut = 0
    for i in range(1, n + 1):
        if FONTS[fs].width(s[:i]) <= maxw:
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


# ---- screens --------------------------------------------------------------
# Live state mirrors f1::loadDemo()
DEMO = dict(circuit="INTERLAGOS", session="RACE", lap=32, total=71, flag="YELLOW",
            air=26.4, track=41.2, hum=62, rain=False,
            rc="YELLOW IN TRACK SECTOR 1", radio="RUS", radio_fresh=True,
            demo=True, clock="20:41", news=("NOR", "1:18.442", "FASTEST"))
ROWS = [
    (1, "VER", "VERSTAPPEN", "LEADER", (54, 113, 198), "M", "4L", False),
    (2, "NOR", "NORRIS", "+2.431", (255, 135, 0), "M", "14L", False),
    (3, "RUS", "RUSSELL", "+6.204", (0, 215, 182), "H", "4L", True),
    (4, "ANT", "ANTONELLI", "+7.881", (0, 215, 182), "S", "14L", True),
    (5, "LEC", "LECLERC", "+14.532", (225, 6, 0), "H", "4L", False),
    (6, "HAM", "HAMILTON", "+15.104", (225, 6, 0), "M", "14L", False),
    (7, "PIA", "PIASTRI", "+21.457", (255, 135, 0), "H", "4L", False),
    (8, "SAI", "SAINZ", "+28.902", (0, 130, 250), "S", "14L", False),
]
COMP = {"S": "SOFT", "M": "MEDIUM", "H": "HARD", "I": "INTER", "W": "WET"}


def live_screen(geo):
    g = geo
    cv = Canvas()
    cv.star(4, 0, 20, C["WHITE"], C["BG"], STAR20)
    cv.text(12, 27, g["hdr_base"], DEMO["circuit"], C["SILVER"], C["BG"])
    cx = 27 + FONTS[12].width(DEMO["circuit"]) + 8
    if cx + FONTS[10].width(DEMO["session"]) < 180:
        cv.text(10, cx, g["hdr_small"], DEMO["session"], C["DIM"], C["BG"])
    lap = "%d/%d" % (DEMO["lap"], DEMO["total"])
    lw = FONTS[12].width(lap)
    cv.text_right(12, 226, g["hdr_base"], lap, C["TEAL"], C["BG"])
    cv.text_right(10, 226 - lw - 4, g["hdr_small"], "LAP", C["DIM"], C["BG"])
    ft = DEMO["flag"]
    fw = FONTS[10].width(ft)
    cv.text_right(10, 316, g["hdr_small"], ft, C["YELLOW"], C["BG"])
    cv.rect(310 - fw - g["swatch_gap"], g["swatch_y"], 6, 6, C["YELLOW"])
    cv.hline(0, 20, 320, C["PETRONAS"])

    rowH, listY = 16, 24
    for i, (pos, code, last, gap, col, comp, age, target) in enumerate(ROWS):
        y = listY + i * rowH
        bg = C["TARGET"] if target else (C["ROW_ALT"] if i & 1 else C["BG"])
        cv.rect(0, y, 176, rowH, bg)
        cv.rect(19, g["bar_y"], 3, g["bar_h"], C["TEAL"] if target else rgb565(*col))
        cv.text_right(12, 16, y + g["row_base"], str(pos),
                      C["TEAL"] if pos == 1 else (C["WHITE"] if target else C["SILVER"]), bg)
        cv.text(16, 25, y + g["code_base"], code, C["WHITE"] if target else C["SILVER"], bg)
        if gap:
            gc = C["TEAL"] if gap == "LEADER" else (C["WHITE"] if target else C["SILVER"])
            cv.text_right(10, g["gap_right"], y + g["row_base"], gap, gc, bg)
        else:
            cv.text(10, 63, y + g["row_base"], last, C["DIM"], bg)
        cv.text(12, g["compound_x"], y + g["row_base"], comp, C[COMP[comp]], bg)
        cv.text_right(10, 172, y + g["row_base"], age, C["DIM"], bg)

    x, w = 182, 134
    cv.vline(176, 24, 127, C["PANEL"])
    cv.text(10, x, g["cond_label"], "CONDITIONS", C["DIM"], C["BG"])
    cv.text(12, x, g["cond_val1"], "TRK %d°   AIR %d°" % (int(DEMO["track"] + .5), int(DEMO["air"] + .5)),
            C["SILVER"], C["BG"])
    cv.text(12, x, g["cond_val2"], "HUM %d%%   %s" % (int(DEMO["hum"]), "WET" if DEMO["rain"] else "DRY"),
            C["DIM"], C["BG"])
    cv.hline(x, g["div1"], w, C["PANEL"])
    cv.text(10, x, g["rc_label"], "RACE CONTROL", C["DIM"], C["BG"])
    a, b = wrap_two(DEMO["rc"], 10, w)
    if a:
        cv.text(10, x, g["rc_l1"], a, C["YELLOW"], C["BG"])
    if b:
        cv.text(10, x, g["rc_l2"], b, C["YELLOW"], C["BG"])
    cv.hline(x, g["div2"], w, C["PANEL"])
    cv.text(10, x, g["radio_label"], "TEAM RADIO", C["DIM"], C["BG"])
    if DEMO["radio_fresh"]:
        cv.rect(x, g["radio_bar_y"], 3, g["radio_bar_h"], C["TEAL"])
        cv.text(16, x + 7, g["radio_code"], DEMO["radio"], C["WHITE"], C["BG"])
        cv.text_right(10, x + w, g["radio_base"], "ON AIR", C["TEAL"], C["BG"])
    else:
        cv.text(10, x, g["radio_base"], "STANDBY", C["DIM"], C["BG"])

    cv.rect(0, 152, 320, 18, C["BG"])
    cv.rect(0, 152, 320, 3, C["PANEL"])
    cv.rect(0, 152, 32 * 320 // 71, 3, C["TEAL"])
    cv.text(10, 6, g["foot_base"], "DEMO" if DEMO["demo"] else "LIVE DATA",
            C["YELLOW"] if DEMO["demo"] else C["DIM"], C["BG"])
    if DEMO["news"]:
        code, tm, label = DEMO["news"]
        c = C["PURPLE"] if label == "FASTEST" else C["PBEST"]
        cv.rect(g["news_bar_x"], g["news_bar_y"], 2, g["news_bar_h"], c)
        cv.text(10, g["news_code"], g["foot_base"], code, C["WHITE"], C["BG"])
        cv.text(12, g["news_time"], g["foot_base"], tm, c, C["BG"])
        cv.text(10, g["news_label"], g["foot_base"], label, C["DIM"], C["BG"])
    left = DEMO["total"] - DEMO["lap"]
    cv.text_right(10, 314, g["foot_base"], "%d LAPS TO GO" % left, C["SILVER"], C["BG"])
    return cv.im


def countdown_screen(geo):
    g = geo
    cv = Canvas()
    cv.star(4, 0, 20, C["WHITE"], C["BG"], STAR20)
    cv.text(12, 27, g["hdr_base"], "NEXT ROUND", C["SILVER"], C["BG"])
    cv.text_right(12, 316, g["hdr_base"], DEMO["clock"], C["DIM"], C["BG"])
    cv.hline(0, 20, 320, C["PETRONAS"])

    cv.text(10, 12, 40, "LIGHTS OUT IN", C["DIM"], C["BG"])
    seg7(cv, 12, g["days_y"], g["days_h"], "14", C["TEAL"], C["SEGOFF"])
    cv.text(12, 84, g["days_label"], "DAYS", C["DIM"], C["BG"])
    seg7(cv, 12, g["time_y"], g["time_h"], "12:34:05", C["TEAL"], C["SEGOFF"])

    x = 150
    cv.text(10, x, 40, "ROUND 21", C["DIM"], C["BG"])
    a, b = wrap_two("SAO PAULO GRAND PRIX", 12, 320 - x - 4)
    cv.text(12, x, g["name1"], a, C["SILVER"], C["BG"])
    if b:
        cv.text(12, x, g["name2"], b, C["SILVER"], C["BG"])
    cv.text(12, x, g["when"], "NOV 09  01:00", C["TEAL"], C["BG"])
    cv.text(10, x, g["when_label"], "LOCAL TIME", C["DIM"], C["BG"])
    cv.hline(x, g["div"], 320 - x - 4, C["PANEL"])
    cv.text(10, x, g["brand"], "MERCEDES-AMG PETRONAS", C["DIM"], C["BG"])
    if DEMO["demo"]:
        cv.text(10, 6, g["demo"], "DEMO", C["YELLOW"], C["BG"])
    return cv.im


def last_screen(geo):
    g = geo
    cv = Canvas()
    cv.star(4, 0, 20, C["WHITE"], C["BG"], STAR20)
    cv.text(12, 27, g["hdr_base"], "LAST RACE", C["SILVER"], C["BG"])
    cv.text_right(12, 316, g["hdr_base"], "MEXICO CITY GRAND PRIX", C["DIM"], C["BG"])
    cv.hline(0, 20, 320, C["PETRONAS"])
    rows = [(1, "VER", "RED BULL RACING", False), (2, "HAM", "FERRARI", False),
            (3, "ANT", "MERCEDES", True), (4, "LEC", "FERRARI", False),
            (5, "NOR", "MCLAREN", False)]
    for i, (pos, code, team, target) in enumerate(rows):
        y = g["row0"] + i * g["row_step"]
        bg = C["TARGET"] if target else C["BG"]
        if target:
            cv.rect(0, y - 2, 196, 22, bg)
        cv.text_right(16, g["pos_right"], y + g["lbase"], str(pos),
                      C["TEAL"] if i == 0 else C["SILVER"], bg)
        cv.text(16, 40, y + g["lbase"], code, C["WHITE"] if target else C["SILVER"], bg)
        cv.text(10, 92, y + g["team_base"], team, C["TEAL"] if target else C["DIM"], bg)
    cv.vline(200, g["row0"], 108, C["PANEL"])
    x = 208
    cv.text(10, x, 40, "NEXT IN", C["DIM"], C["BG"])
    cv.text(22, x, g["next_base"], "14D 12H", C["TEAL"], C["BG"])
    a, b = wrap_two("LAS VEGAS GRAND PRIX", 10, 320 - x - 4)
    cv.text(10, x, g["next_name1"], a, C["SILVER"], C["BG"])
    if b:
        cv.text(10, x, g["next_name2"], b, C["SILVER"], C["BG"])
    cv.text(10, x, g["next_when"], "NOV 22  04:00", C["DIM"], C["BG"])
    cv.rect(0, 166, 320, 4, C["PETRONAS"])
    cv.text(10, 6, g["foot_base"], "MERCEDES-AMG PETRONAS", C["DIM"], C["BG"])
    if DEMO["demo"]:
        cv.text_right(10, 314, g["foot_base"], "DEMO", C["YELLOW"], C["BG"])
    return cv.im


def splash_screen(geo):
    g = geo
    cv = Canvas()
    team_logo(cv, 14, 10, 140, C["BG"])
    cv.text(22, g["splash_x"], g["title"], "LIVE TIMING", C["SILVER"], C["BG"])
    cv.rect(g["splash_x"], g["rule"], 60, 2, C["PETRONAS"])
    cv.text_styled(10, g["splash_x"], g["sub"], "MERCEDES-AMG PETRONAS", C["DIM"],
                   C["BG"], g["sub_scale"], 0)
    cv.rect(0, 166, 320, 4, C["PETRONAS"])
    return cv.im


# Mirrors ui.cpp: every value is the number the firmware passes to drawText /
# fillRect, so the render is the panel, not an approximation of it.
GEO = dict(
    hdr_base=15, hdr_small=15, swatch_y=8, swatch_gap=4,
    row_base=14, code_base=14, bar_y=2, bar_h=12, gap_right=132, compound_x=138,
    cond_label=34, cond_val1=50, cond_val2=63, div1=70,
    rc_label=81, rc_l1=94, rc_l2=105, div2=116,
    radio_label=127, radio_bar_y=133, radio_bar_h=12, radio_code=145,
    radio_base=145, foot_base=166,
    news_bar_x=70, news_bar_y=158, news_bar_h=8, news_code=77, news_time=102,
    news_label=158,
    days_y=46, days_h=52, days_label=98, time_y=106, time_h=26,
    name1=60, name2=74, when=96, when_label=109, div=118, brand=136, demo=162,
    row0=28, row_step=22, pos_right=28, lbase=16, team_base=16,
    next_base=66, next_name1=86, next_name2=98, next_when=120,
    splash_x=176, title=66, rule=78, sub=100, sub_scale=0.92)


def sheet(geo, out, scale=2):
    screens = [("live", live_screen(geo)), ("countdown", countdown_screen(geo)),
               ("last race", last_screen(geo)), ("splash", splash_screen(geo))]
    sh = Image.new("RGB", (W * scale + 16, (H * scale + 20) * len(screens) + 8),
                   (40, 40, 40))
    y = 0
    for name, im in screens:
        sh.paste(im.resize((W * scale, H * scale), Image.NEAREST), (8, y + 8))
        y += H * scale + 20
        im.save(os.path.join(HERE, "screen_%s.png" % name.replace(" ", "_")))
    sh.save(out)
    print("wrote", out, sh.size)


if __name__ == "__main__":
    sheet(GEO, os.path.join(HERE, "ui_panel.png"))
