#pragma once
#include <TFT_eSPI.h>
#include "config.h"

struct Glyph {
  uint16_t cp;
  uint8_t w, h;
  int8_t xo, yo;
  uint8_t adv;
  uint16_t off;
};

struct BitmapFont {
  const Glyph* glyphs;
  const uint8_t* alpha;
  uint8_t count;
  int8_t ascent, descent;
};

struct StarMask {
  uint8_t w, h;
  const uint8_t* alpha;  // 4bpp, high nibble first
};

#include "assets.h"

constexpr uint16_t C_BG       = rgb(13, 13, 13);
constexpr uint16_t C_PANEL    = rgb(22, 22, 22);
constexpr uint16_t C_TEAL     = rgb(0, 215, 182);
constexpr uint16_t C_PETRONAS = rgb(0, 161, 155);
constexpr uint16_t C_SILVER   = rgb(200, 200, 200);
constexpr uint16_t C_DIM      = rgb(110, 110, 110);
constexpr uint16_t C_WHITE    = rgb(255, 255, 255);
constexpr uint16_t C_RED      = rgb(225, 6, 0);
constexpr uint16_t C_YELLOW   = rgb(255, 215, 0);
constexpr uint16_t C_ROW_ALT  = rgb(17, 17, 17);
constexpr uint16_t C_TARGET   = rgb(7, 28, 25);
constexpr uint16_t C_SOFT     = rgb(255, 51, 51);
constexpr uint16_t C_MEDIUM   = rgb(255, 215, 0);
constexpr uint16_t C_HARD     = rgb(255, 255, 255);
constexpr uint16_t C_INTER    = rgb(57, 181, 74);
constexpr uint16_t C_WET      = rgb(0, 160, 222);
constexpr uint16_t C_PURPLE   = rgb(168, 40, 240);  // session best lap
constexpr uint16_t C_PBEST    = rgb(60, 220, 90);   // personal best lap

extern const BitmapFont& F10;
extern const BitmapFont& F12;
extern const BitmapFont& F16;
extern const BitmapFont& F22;
extern const BitmapFont& F28;
extern const StarMask& STAR20;
extern const StarMask& STAR44;
extern const StarMask& STAR120;

uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a);
void drawStar(TFT_eSPI& dst, const StarMask& m, int x, int y, uint16_t fg,
              uint16_t bg, uint8_t opacity = 255);
int textWidth(const BitmapFont& f, const char* s);
void drawText(TFT_eSPI& dst, const BitmapFont& f, int x, int baseline,
              const char* s, uint16_t fg, uint16_t bg);
void drawTextStyled(TFT_eSPI& dst, const BitmapFont& f, int x, int baseline,
                    const char* s, uint16_t fg, uint16_t bg, float xScale,
                    int tracking);
void drawTextRight(TFT_eSPI& dst, const BitmapFont& f, int rightX, int baseline,
                   const char* s, uint16_t fg, uint16_t bg);
void drawTextCenter(TFT_eSPI& dst, const BitmapFont& f, int cx, int baseline,
                    const char* s, uint16_t fg, uint16_t bg);
int segWidth(int h, const char* s);
int draw7SegString(TFT_eSPI& dst, int x, int y, int h, const char* s,
                   uint16_t on, uint16_t off);
void upperCopy(char* dst, const char* src, size_t n);
