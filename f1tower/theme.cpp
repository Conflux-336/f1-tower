#include "theme.h"
#include <string.h>

const BitmapFont& F10 = font10;
const BitmapFont& F12 = font12;
const BitmapFont& F16 = font16;
const BitmapFont& F22 = font22;
const BitmapFont& F28 = font28;
const StarMask& STAR20 = star20;
const StarMask& STAR44 = star44;
const StarMask& STAR120 = star120;

uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  if (a == 0) return bg;
  if (a == 255) return fg;
  uint32_t inv = 255 - a;
  uint16_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * inv) / 255;
  uint16_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * inv) / 255;
  uint16_t b = ((fg & 0x1F) * a + (bg & 0x1F) * inv) / 255;
  return (r << 11) | (g << 5) | b;
}

static uint16_t nextCp(const char*& s) {
  uint8_t c = (uint8_t)*s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) return ((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
  if ((c & 0xF0) == 0xE0) {
    s += 2;
    return '?';
  }
  return '?';
}

static const Glyph* findGlyph(const BitmapFont& f, uint16_t cp) {
  for (uint8_t i = 0; i < f.count; i++) {
    if (f.glyphs[i].cp == cp) return &f.glyphs[i];
  }
  return nullptr;
}

int textWidth(const BitmapFont& f, const char* s) {
  int w = 0;
  const char* p = s;
  while (*p) {
    const Glyph* g = findGlyph(f, nextCp(p));
    if (g) w += g->adv;
  }
  return w;
}

void drawText(TFT_eSPI& dst, const BitmapFont& f, int x, int baseline,
              const char* s, uint16_t fg, uint16_t bg) {
  int pen = x;
  const char* p = s;
  while (*p) {
    const Glyph* g = findGlyph(f, nextCp(p));
    if (!g) continue;
    const uint8_t* a = f.alpha + g->off;
    int gx = pen + g->xo;
    int gy = baseline + g->yo;
    for (int yy = 0; yy < g->h; yy++) {
      for (int xx = 0; xx < g->w; xx++) {
        uint8_t al = a[yy * g->w + xx];
        if (al) dst.drawPixel(gx + xx, gy + yy, blend565(fg, bg, al));
      }
    }
    pen += g->adv;
  }
}

void drawTextRight(TFT_eSPI& dst, const BitmapFont& f, int rightX, int baseline,
                   const char* s, uint16_t fg, uint16_t bg) {
  drawText(dst, f, rightX - textWidth(f, s), baseline, s, fg, bg);
}

// Horizontal stretch + letter tracking. Each destination pixel box-filters the
// source alpha it covers, so extended text keeps a single soft edge instead of
// duplicated columns.
void drawTextStyled(TFT_eSPI& dst, const BitmapFont& f, int x, int baseline,
                    const char* s, uint16_t fg, uint16_t bg, float xScale,
                    int tracking) {
  if (xScale < 0.2f) xScale = 0.2f;
  int pen = x;
  const char* p = s;
  while (*p) {
    const Glyph* g = findGlyph(f, nextCp(p));
    if (!g) continue;
    const uint8_t* a = f.alpha + g->off;
    float gx = pen + g->xo * xScale;
    for (int row = 0; row < g->h; row++) {
      int y = baseline + g->yo + row;
      const uint8_t* src = a + row * g->w;
      int px0 = (int)floorf(gx);
      int px1 = (int)ceilf(gx + g->w * xScale);
      for (int px = px0; px < px1; px++) {
        float u0 = (px - gx) / xScale;
        float u1 = (px + 1 - gx) / xScale;
        int acc = 0;
        for (int k = 0; k < 3; k++) {
          float u = u0 + (u1 - u0) * (0.5f + k) / 3.0f;
          int ui = (int)floorf(u);
          float fu = u - ui;
          if (ui >= 0 && ui < g->w) acc += (int)(src[ui] * (1.0f - fu));
          if (ui + 1 >= 0 && ui + 1 < g->w) acc += (int)(src[ui + 1] * fu);
        }
        uint8_t al = (uint8_t)(acc / 3);
        if (al) dst.drawPixel(px, y, blend565(fg, bg, al));
      }
    }
    pen += (int)(g->adv * xScale + 0.5f) + tracking;
  }
}

void drawTextCenter(TFT_eSPI& dst, const BitmapFont& f, int cx, int baseline,
                    const char* s, uint16_t fg, uint16_t bg) {
  drawText(dst, f, cx - textWidth(f, s) / 2, baseline, s, fg, bg);
}

void drawStar(TFT_eSPI& dst, const StarMask& m, int x, int y, uint16_t fg,
              uint16_t bg, uint8_t opacity) {
  const uint8_t* p = m.alpha;
  for (int row = 0; row < m.h; row++) {
    for (int col = 0; col < m.w; col += 2) {
      uint8_t byte = *p++;
      uint8_t a0 = (byte >> 4) * 17;
      uint8_t a1 = (byte & 0x0F) * 17;
      if (a0) {
        uint8_t a = (uint16_t)a0 * opacity / 255;
        dst.drawPixel(x + col, y + row, blend565(fg, bg, a));
      }
      if (a1 && col + 1 < m.w) {
        uint8_t a = (uint16_t)a1 * opacity / 255;
        dst.drawPixel(x + col + 1, y + row, blend565(fg, bg, a));
      }
    }
  }
}

// ---- seven segment --------------------------------------------------------
static const uint8_t DIGIT_SEG[10] = {
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111};

static int segGeom(int h, int& w, int& t, int& gap) {
  w = h * 55 / 100;
  t = h >= 30 ? 4 : 3;
  gap = h >= 30 ? 6 : 4;
  return 0;
}

static void segRect(TFT_eSPI& d, int x, int y, int w, int h, bool on,
                    uint16_t onC, uint16_t offC) {
  d.fillRect(x, y, w, h, on ? onC : offC);
}

static void drawDigit(TFT_eSPI& d, int x, int y, int w, int h, uint8_t mask,
                      uint16_t onC, uint16_t offC) {
  int t, gap;
  int ww;
  segGeom(h, ww, t, gap);
  (void)ww;
  int half = h / 2;
  // a top, g middle, d bottom
  segRect(d, x + t, y, w - 2 * t, t, mask & 0x01, onC, offC);
  segRect(d, x + t, y + half - t / 2, w - 2 * t, t, mask & 0x40, onC, offC);
  segRect(d, x + t, y + h - t, w - 2 * t, t, mask & 0x08, onC, offC);
  // f top-left, e bottom-left, b top-right, c bottom-right
  int vh = half - t - t / 2;
  segRect(d, x, y + t, t, vh, mask & 0x20, onC, offC);
  segRect(d, x + w - t, y + t, t, vh, mask & 0x02, onC, offC);
  segRect(d, x, y + half + t / 2, t, vh, mask & 0x10, onC, offC);
  segRect(d, x + w - t, y + half + t / 2, t, vh, mask & 0x04, onC, offC);
}

int segWidth(int h, const char* s) {
  int w, t, gap;
  segGeom(h, w, t, gap);
  int total = 0;
  for (const char* p = s; *p; p++) {
    if (*p == ':') total += t + gap;
    else if (*p == ' ') total += gap * 2;
    else total += w + gap;
  }
  if (total) total -= gap;
  return total;
}

int draw7SegString(TFT_eSPI& d, int x, int y, int h, const char* s,
                   uint16_t onC, uint16_t offC) {
  int w, t, gap;
  segGeom(h, w, t, gap);
  int pen = x;
  for (const char* p = s; *p; p++) {
    if (*p == ':') {
      d.fillRect(pen, y + h / 2 - t - 1, t, t, onC);
      d.fillRect(pen, y + h / 2 + 1, t, t, onC);
      pen += t + gap;
    } else if (*p == ' ') {
      pen += gap * 2;
    } else if (*p >= '0' && *p <= '9') {
      drawDigit(d, pen, y, w, h, DIGIT_SEG[*p - '0'], onC, offC);
      pen += w + gap;
    }
  }
  return pen - x - gap;
}

void upperCopy(char* dst, const char* src, size_t n) {
  if (!n) return;
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) {
    char c = src[i];
    dst[i] = (c >= 'a' && c <= 'z') ? c - 32 : c;
  }
  dst[i] = 0;
}
