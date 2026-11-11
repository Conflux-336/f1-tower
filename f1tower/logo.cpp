#include "logo.h"
#include <math.h>
#include <string.h>
#include "amgmark.h"
#include "theme.h"

static constexpr float PI_F = 3.14159265f;

// Arm cross-section measured off the official mark: notch at 0.166R / 60deg,
// tip half-width 0.043 x tip radius, ring band 0.91R..R.
static bool inArm(float px, float py, float rTip, float rNotch, float tipHalf) {
  const float nx = rNotch * 0.8660254f;
  const float ny = rNotch * 0.5f;
  const float vx[5] = {0, nx, tipHalf, -tipHalf, -nx};
  const float vy[5] = {0, ny, rTip, rTip, ny};
  for (int i = 0; i < 5; i++) {
    int j = (i + 1) % 5;
    float ex = vx[j] - vx[i], ey = vy[j] - vy[i];
    float wx = px - vx[i], wy = py - vy[i];
    if (ex * wy - ey * wx < 0) return false;
  }
  return true;
}

static float gSa[3], gCa[3];

static bool inStar(float dx, float dy, float R) {
  float d2 = dx * dx + dy * dy;
  if (d2 > R * R) return false;
  float rIn = R * 0.91f;
  if (d2 >= rIn * rIn) return true;  // ring band
  float rNotch = R * 0.166f;
  float tipHalf = rIn * 0.043f;
  for (int i = 0; i < 3; i++) {
    float sa = gSa[i], ca = gCa[i];
    if (inArm(dx * sa + dy * ca, dx * ca - dy * sa, rIn, rNotch, tipHalf))
      return true;
  }
  return false;
}

// 3x3 supersampled coverage; no mask table, so it stays smooth at any size.
static void drawStarAA(TFT_eSPI& tft, float cx, float cy, float R, uint16_t fg,
                       uint16_t bg) {
  for (int i = 0; i < 3; i++) {
    float a = (90.0f + 120.0f * i) * PI_F / 180.0f;
    gSa[i] = sinf(a);
    gCa[i] = cosf(a);
  }
  int x0 = (int)floorf(cx - R) - 1, x1 = (int)ceilf(cx + R);
  int y0 = (int)floorf(cy - R) - 1, y1 = (int)ceilf(cy + R);
  const int SS = 3;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      int hit = 0;
      for (int sy = 0; sy < SS; sy++) {
        for (int sx = 0; sx < SS; sx++) {
          float fx = x + (sx + 0.5f) / SS - cx;
          float fy = y + (sy + 0.5f) / SS - cy;
          if (inStar(fx, fy, R)) hit++;
        }
      }
      if (hit) {
        uint8_t a = (uint8_t)(hit * 255 / (SS * SS));
        tft.drawPixel(x, y, blend565(fg, bg, a));
      }
    }
  }
}

// Slashes + A M G, traced from the official mark. Scanline even-odd fill with
// three sub-rows per pixel for vertical AA; horizontal edges are exact.
static void drawAmgMark(TFT_eSPI& tft, int x, int y, int width, uint16_t fg,
                        uint16_t bg) {
  if (width < 8 || width > 320) return;
  const float s = width / (float)AMG_W;
  int h = (int)lroundf(AMG_H * s);
  static uint16_t acc[320];
  static float xs[64];
  const int SUB = 3;

  for (int j = 0; j < h; j++) {
    memset(acc, 0, sizeof(uint16_t) * width);
    for (int k = 0; k < SUB; k++) {
      float ym = (j + (k + 0.5f) / SUB) / s;
      int n = 0;
      for (int p = 0; p < AMG_POLY_COUNT; p++) {
        const AmgPoly& po = AMG_POLYS[p];
        const int16_t* v = AMG_PTS + po.first * 2;
        for (int i = 0; i < po.n; i++) {
          int i2 = (i + 1 == po.n) ? 0 : i + 1;
          float y1 = v[i * 2 + 1], y2 = v[i2 * 2 + 1];
          if ((y1 <= ym && y2 > ym) || (y2 <= ym && y1 > ym)) {
            float x1 = v[i * 2], x2 = v[i2 * 2];
            if (n < 64) xs[n++] = x1 + (ym - y1) * (x2 - x1) / (y2 - y1);
          }
        }
      }
      for (int a = 1; a < n; a++) {
        float v = xs[a];
        int b = a - 1;
        while (b >= 0 && xs[b] > v) {
          xs[b + 1] = xs[b];
          b--;
        }
        xs[b + 1] = v;
      }
      for (int a = 0; a + 1 < n; a += 2) {
        float lo = xs[a] * s, hi = xs[a + 1] * s;
        if (hi <= 0 || lo >= width) continue;
        if (lo < 0) lo = 0;
        if (hi > width) hi = width;
        int p0 = (int)lo, p1 = (int)ceilf(hi) - 1;
        for (int px = p0; px <= p1; px++) {
          float cov = fminf(hi, px + 1.0f) - fmaxf(lo, (float)px);
          if (cov > 0) acc[px] += (uint16_t)(cov * 85.0f);
        }
      }
    }
    for (int px = 0; px < width; px++) {
      if (!acc[px]) continue;
      uint8_t al = acc[px] > 255 ? 255 : (uint8_t)acc[px];
      tft.drawPixel(x + px, y + j, blend565(fg, bg, al));
    }
  }
}

// Nearest font for a target cap height, so the wordmark keeps its proportions
// whatever width the caller asks for.
static const BitmapFont& fontForCap(float cap) {
  struct Entry {
    const BitmapFont* f;
    float cap;
  };
  static const Entry table[] = {
      {&F10, 7.2f}, {&F12, 8.6f}, {&F16, 11.6f}, {&F22, 16.0f}, {&F28, 20.3f}};
  const BitmapFont* best = &F16;
  float bd = 1e9f;
  for (const Entry& e : table) {
    float d = fabsf(e.cap - cap);
    if (d < bd) {
      bd = d;
      best = e.f;
    }
  }
  return *best;
}

int drawTeamLogo(TFT_eSPI& tft, int x, int top, int width, uint16_t bg) {
  const float s = width / (float)AMG_W;
  tft.startWrite();
  drawStarAA(tft, x + width * 0.5f, top + 100.0f * s, 100.5f * s, C_WHITE, bg);
  drawAmgMark(tft, x, top + (int)(241 * s), width, C_WHITE, bg);

  const BitmapFont& pf = fontForCap(34 * s);
  int pw = textWidth(pf, "PETRONAS");
  if (pw > 0)
    drawTextStyled(tft, pf, x, top + (int)(322 * s), "PETRONAS", C_PETRONAS, bg,
                   (float)width / pw, 0);

  const BitmapFont& ff = fontForCap(23 * s);
  const char* f1 = "FORMULA 1 TEAM";
  int f1w = textWidth(ff, f1);
  int gap = (width - f1w) / 13;
  if (gap < 0) gap = 0;
  drawTextStyled(tft, ff, x + (width - f1w - 13 * gap) / 2,
                 top + (int)(357 * s), f1, C_WHITE, bg, 1.0f, gap);

  tft.endWrite();
  return (int)(357 * s);
}
