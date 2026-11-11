import math
from PIL import Image, ImageDraw, ImageFont

W_PANEL, H_PANEL = 170, 320
BG = (8, 12, 8)
WHITE = (255, 255, 255)
TEAL = (0, 159, 159)
SILVER = (200, 200, 200)
DIM = (104, 104, 104)
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"


def in_arm(px, py, r_tip, r_notch, tip_half):
    nx, ny = r_notch * 0.8660254, r_notch * 0.5
    vx = [0, nx, tip_half, -tip_half, -nx]
    vy = [0, ny, r_tip, r_tip, ny]
    for i in range(5):
        j = (i + 1) % 5
        ex, ey = vx[j] - vx[i], vy[j] - vy[i]
        wx, wy = px - vx[i], py - vy[i]
        if ex * wy - ey * wx < 0:
            return False
    return True


def in_star(dx, dy, R):
    d2 = dx * dx + dy * dy
    if d2 > R * R:
        return False
    r_in = R * 0.91
    if d2 >= r_in * r_in:
        return True
    r_notch = R * 0.166
    tip_half = r_in * 0.043
    for i in range(3):
        a = math.radians(90 + 120 * i)
        sa, ca = math.sin(a), math.cos(a)
        if in_arm(dx * sa + dy * ca, dx * ca - dy * sa, r_in, r_notch, tip_half):
            return True
    return False


def draw_star(img, cx, cy, R, color):
    px = img.load()
    SS = 3
    for y in range(int(cy - R) - 1, int(cy + R) + 2):
        for x in range(int(cx - R) - 1, int(cx + R) + 2):
            hit = 0
            for sy in range(SS):
                for sx in range(SS):
                    fx = x + (sx + 0.5) / SS - cx
                    fy = y + (sy + 0.5) / SS - cy
                    if in_star(fx, fy, R):
                        hit += 1
            if hit:
                a = hit / (SS * SS)
                px[x, y] = tuple(round(color[i] * a + BG[i] * (1 - a)) for i in range(3))


def render(width=158, x0=6, top=34):
    s = width / 337.0
    img = Image.new("RGB", (W_PANEL, H_PANEL), BG)
    draw_star(img, x0 + width / 2, top + 100 * s, 100.5 * s, WHITE)
    d = ImageDraw.Draw(img)

    amg_base = top + int(273 * s)
    cap_h = int(33 * s)
    slash_lean = 0.75
    bar_w = [0.089, 0.0475, 0.0386, 0.0297]
    bar_g = [0.012, 0.018, 0.021]
    pen = float(x0)
    for i in range(4):
        bw = max(2, round(bar_w[i] * width))
        bx = int(pen)
        for r in range(cap_h):
            off = round(slash_lean * (cap_h - 1 - r))
            d.rectangle([bx + off, amg_base - cap_h + r, bx + off + bw - 1,
                         amg_base - cap_h + r], fill=WHITE)
        pen += bw
        if i < 3:
            pen += bar_g[i] * width
    amg_x = int(pen) + round(slash_lean * cap_h) + 2

    def styled(text, font_px, x, base, color, xscale, shear, tracking):
        f = ImageFont.truetype(FONT, font_px)
        layer = Image.new("L", (600, 80), 0)
        ld = ImageDraw.Draw(layer)
        ld.text((100, 60), text, font=f, fill=255, anchor="ls")
        bb = layer.getbbox()
        g = layer.crop(bb)
        w2 = max(1, int(g.width * xscale))
        g = g.resize((w2, g.height), Image.LANCZOS)
        sh = int(shear * g.height) + 1
        g = g.transform((w2 + sh, g.height), Image.AFFINE,
                        (1, -shear, 0, 0, 1, 0), resample=Image.BICUBIC)
        img.paste(Image.new("RGB", g.size, color), (x, base - g.height + 3), g)

    tracking = int(2 * s) + 1
    base_len = ImageFont.truetype(FONT, 22).getlength("AMG")
    xs = (x0 + width - amg_x - 2 * tracking) / base_len
    styled("AMG", 22, amg_x, amg_base, WHITE, xs, 0.18, tracking)

    f28 = ImageFont.truetype(FONT, 28)
    d.text((x0 + (width - f28.getlength("PETRONAS")) / 2, top + int(331 * s)),
           "PETRONAS", font=f28, fill=TEAL, anchor="ls")

    f16 = ImageFont.truetype(FONT, 16)
    t1 = "FORMULA 1 TEAM"
    gap = (width - int(f16.getlength(t1))) // 13
    tx = x0 + (width - f16.getlength(t1) - 13 * gap) / 2
    for ch in t1:
        d.text((tx, top + int(369 * s)), ch, font=f16, fill=WHITE, anchor="ls")
        tx += f16.getlength(ch) + gap

    d.rectangle([60, top + int(370 * s) + 18, 109, top + int(370 * s) + 19],
                fill=TEAL)
    f16b = ImageFont.truetype(FONT, 16)
    f10b = ImageFont.truetype(FONT, 10)
    for t, f, y, c in (("LIVE TIMING", f16b, 254, SILVER),
                       ("MERCEDES-AMG PETRONAS", f10b, 276, DIM)):
        d.text(((W_PANEL - d.textlength(t, font=f)) / 2, y), t, font=f, fill=c,
               anchor="ls")
    d.rectangle([0, 314, W_PANEL - 1, H_PANEL - 1], fill=TEAL)
    return img


if __name__ == "__main__":
    im = render()
    im.resize((W_PANEL * 3, H_PANEL * 3), Image.NEAREST).save("/tmp/vector_logo2.png")
    im.save("/tmp/vector_logo_1x.png")
    print("ok")
