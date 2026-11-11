#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "f1data.h"
#include "logo.h"
#include "theme.h"

extern TFT_eSPI tft;
static TFT_eSprite rowSprite(&tft);

static uint32_t lastSig = 0;
static long paintedSession = -1;
static bool livePainted = false;
static bool idleCountdown = true;
static bool demoTag = false;
static time_t idleLastRem = -1;
static bool newsOn = false;
static uint16_t newsShown = 0;  // seq of the item on screen
static uint32_t newsAt = 0;

// ---------------------------------------------------------------------------
static uint16_t compoundColour(char c) {
  switch (c) {
    case 'S': return C_SOFT;
    case 'M': return C_MEDIUM;
    case 'H': return C_HARD;
    case 'I': return C_INTER;
    case 'W': return C_WET;
    default: return C_DIM;
  }
}

static const char* flagText(uint8_t f) {
  switch (f) {
    case FLAG_YELLOW: return "YELLOW";
    case FLAG_SC: return "SAFETY CAR";
    case FLAG_VSC: return "VSC";
    case FLAG_RED: return "RED FLAG";
    case FLAG_CHEQUERED: return "FINISH";
    default: return "TRACK CLEAR";
  }
}

static uint16_t flagColour(uint8_t f) {
  switch (f) {
    case FLAG_YELLOW: return C_YELLOW;
    case FLAG_SC:
    case FLAG_VSC: return C_YELLOW;
    case FLAG_RED: return C_RED;
    case FLAG_CHEQUERED: return C_WHITE;
    default: return C_TEAL;
  }
}

static bool radioFresh() {
  return f1::live.radioCode[0] && f1::live.radioAtMs &&
         millis() - f1::live.radioAtMs < 8000;
}

// Fastest/personal-best laps ride the footer for a few seconds each and queue
// up like radio calls instead of staying on screen.
static void newsTick() {
  auto& L = f1::live;
  uint32_t now = millis();
  if (L.newsSeq < newsShown) {  // session reset
    newsShown = 0;
    newsOn = false;
  }
  if (newsOn && now - newsAt >= NEWS_HOLD_MS) newsOn = false;
  if (newsOn || L.newsSeq <= newsShown) return;

  uint16_t next = newsShown + 1;
  // A burst can outrun the ring; then jump to the newest item still held.
  newsShown = L.news[next % NEWS_SLOTS].seq == next ? next : L.newsSeq;
  newsOn = true;
  newsAt = now;
}

static const LapNews& newsItem() {
  return f1::live.news[newsShown % NEWS_SLOTS];
}

// Split into at most two lines at a space, honouring a pixel width.
static void wrapTwo(const char* s, char* a, char* b, size_t cap,
                    const BitmapFont& f, int maxW) {
  a[0] = b[0] = 0;
  if (!s || !*s) return;
  char buf[96];
  size_t n = strlen(s);
  if (n > cap - 1) n = cap - 1;
  if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
  memcpy(buf, s, n);
  buf[n] = 0;

  size_t cut = 0;
  char tmp[96];
  for (size_t i = 1; i <= n; i++) {
    memcpy(tmp, buf, i);
    tmp[i] = 0;
    if (textWidth(f, tmp) <= maxW)
      cut = i;
    else
      break;
  }
  size_t brk = cut;
  if (cut < n) {
    for (size_t i = cut; i > 0; i--)
      if (buf[i - 1] == ' ') {
        brk = i - 1;
        break;
      }
  }
  if (brk == 0) brk = cut;
  memcpy(a, buf, brk);
  a[brk] = 0;
  size_t j = brk;
  while (buf[j] == ' ') j++;
  if (j < n) {
    strncpy(b, buf + j, cap - 1);
    b[cap - 1] = 0;
  }
}

// ---------------------------------------------------------------------------
void ui::init() {
  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(C_BG);
  rowSprite.setColorDepth(16);
  if (!rowSprite.createSprite(COL_L, ROW_H)) {
    Serial.println("[ui] row sprite alloc failed");
  }
  rowSprite.setTextDatum(TL_DATUM);
}

void ui::backlight(uint8_t level) {
  static bool attached = false;
  if (!attached) {
    ledcAttach(BACKLIGHT_PIN, 5000, 8);
    attached = true;
  }
  ledcWrite(BACKLIGHT_PIN, level);
}

void ui::setDemo(bool on) { demoTag = on; }

void ui::splash(const char* title, const char* sub) {
  tft.fillScreen(C_BG);
  drawTeamLogo(tft, 14, 10, 140, C_BG);

  char buf[32];
  upperCopy(buf, title, sizeof(buf));
  drawText(tft, F22, 176, 66, buf, C_SILVER, C_BG);
  tft.fillRect(176, 78, 60, 2, C_PETRONAS);
  upperCopy(buf, sub, sizeof(buf));
  drawTextStyled(tft, F10, 176, 100, buf, C_DIM, C_BG, 0.92f, 0);

  tft.fillRect(0, TFT_H - 4, TFT_W, 4, C_PETRONAS);
}

void ui::error(const char* line1, const char* line2) {
  tft.fillScreen(C_BG);
  drawStar(tft, STAR44, 18, 22, C_RED, C_BG, 60);
  drawText(tft, F16, 84, 72, line1, C_SILVER, C_BG);
  drawText(tft, F10, 84, 94, line2, C_DIM, C_BG);
  tft.fillRect(0, TFT_H - 4, TFT_W, 4, C_RED);
}

// ---------------------------------------------------------------------------
static void drawHeader() {
  auto& L = f1::live;
  tft.fillRect(0, 0, TFT_W, HEADER_H, C_BG);
  drawStar(tft, STAR20, 4, 0, C_WHITE, C_BG);

  char buf[24];
  upperCopy(buf, L.circuit, sizeof(buf));
  drawText(tft, F12, 27, 15, buf, C_SILVER, C_BG);
  int cx = 27 + textWidth(F12, buf) + 8;

  char sn[16];
  upperCopy(sn, L.sessionName, sizeof(sn));
  if (cx + textWidth(F10, sn) < 180) drawText(tft, F10, cx, 15, sn, C_DIM, C_BG);

  char lap[12];
  if (L.totalLaps > 0)
    snprintf(lap, sizeof(lap), "%d/%d", L.lap, L.totalLaps);
  else if (L.lap > 0)
    snprintf(lap, sizeof(lap), "%d", L.lap);
  else
    strcpy(lap, "--");
  int lw = textWidth(F12, lap);
  drawTextRight(tft, F12, 226, 15, lap, C_TEAL, C_BG);
  drawTextRight(tft, F10, 226 - lw - 4, 15, "LAP", C_DIM, C_BG);

  uint16_t fc = flagColour(L.flag);
  const char* ft = flagText(L.flag);
  int fw = textWidth(F10, ft);
  drawTextRight(tft, F10, TFT_W - 4, 15, ft, fc, C_BG);
  tft.fillRect(TFT_W - 14 - fw, 8, 6, 6, fc);

  tft.drawFastHLine(0, HEADER_H - 1, TFT_W, C_PETRONAS);
}

static void drawRow(int y, const DriverInfo& d, int pos, bool target, bool alt) {
  uint16_t bg = target ? C_TARGET : (alt ? C_ROW_ALT : C_BG);
  rowSprite.fillSprite(bg);

  char buf[8];
  if (pos > 0)
    snprintf(buf, sizeof(buf), "%d", pos);
  else
    strcpy(buf, "-");
  uint16_t posCol = pos == 1 ? C_TEAL : (target ? C_WHITE : C_SILVER);
  drawTextRight(rowSprite, F12, 16, 14, buf, posCol, bg);

  rowSprite.fillRect(19, 2, 3, 12, target ? C_TEAL : d.colour);
  drawText(rowSprite, F16, 25, 14, d.code, target ? C_WHITE : C_SILVER, bg);
  if (d.gap[0]) {
    uint16_t gc = d.gapLeader ? C_TEAL : (target ? C_WHITE : C_SILVER);
    drawTextRight(rowSprite, F10, 132, 14, d.gap, gc, bg);
  } else if (d.last[0]) {
    drawText(rowSprite, F10, 63, 14, d.last, C_DIM, bg);
  }

  if (d.compound) {
    char cl[2] = {d.compound, 0};
    drawText(rowSprite, F12, 138, 14, cl, compoundColour(d.compound), bg);
  }
  if (d.lapStart && f1::live.lap > d.lapStart) {
    char age[8];
    snprintf(age, sizeof(age), "%dL", f1::live.lap - d.lapStart);
    drawTextRight(rowSprite, F10, 172, 14, age, C_DIM, bg);
  }
  rowSprite.pushSprite(0, y);
}

static int buildShow(int* slots) {
  auto& L = f1::live;
  int order[24];
  int n = 0;
  for (int i = 0; i < L.driverCount; i++)
    if (L.pos[i] > 0) order[n++] = i;
  for (int i = 1; i < n; i++) {
    int v = order[i];
    int j = i - 1;
    while (j >= 0 && L.pos[order[j]] > L.pos[v]) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = v;
  }

  int target = -1;
  for (int i = 0; i < L.driverCount; i++)
    if (L.drivers[i].target) target = i;

  int c = 0;
  const int top = ROWS_VIS - 1;
  for (int i = 0; i < n && c < top; i++) slots[c++] = order[i];
  bool shown = false;
  for (int i = 0; i < c; i++)
    if (slots[i] == target) shown = true;
  if (!shown && target >= 0 && c < ROWS_VIS) {
    slots[c++] = target;
  } else {
    for (int i = top; i < n && c < ROWS_VIS; i++) slots[c++] = order[i];
  }
  return c;
}

static void drawPanels() {
  auto& L = f1::live;
  const int x = COL_L + 6;
  const int w = TFT_W - x - 4;
  tft.fillRect(COL_L, BODY_Y, TFT_W - COL_L, FOOTER_Y - BODY_Y, C_BG);
  tft.drawFastVLine(COL_L, BODY_Y, FOOTER_Y - BODY_Y, C_PANEL);

  char buf[40];
  drawText(tft, F10, x, 34, "CONDITIONS", C_DIM, C_BG);
  if (L.haveWeather) {
    snprintf(buf, sizeof(buf), "TRK %d°   AIR %d°", (int)(L.track + 0.5f),
             (int)(L.air + 0.5f));
    drawText(tft, F12, x, 50, buf, C_SILVER, C_BG);
    snprintf(buf, sizeof(buf), "HUM %d%%   %s", (int)(L.humidity + 0.5f),
             L.rain ? "WET" : "DRY");
    drawText(tft, F12, x, 63, buf, L.rain ? C_WET : C_DIM, C_BG);
  } else {
    drawText(tft, F12, x, 50, "NO WEATHER", C_DIM, C_BG);
  }
  tft.drawFastHLine(x, 70, w, C_PANEL);

  drawText(tft, F10, x, 81, "RACE CONTROL", C_DIM, C_BG);
  char l1[64], l2[64];
  wrapTwo(L.rcMsg, l1, l2, sizeof(l1), F10, w);
  bool hot = L.flag == FLAG_YELLOW || L.flag == FLAG_SC || L.flag == FLAG_VSC;
  uint16_t mc = L.flag == FLAG_RED ? C_RED : (hot ? C_YELLOW : C_SILVER);
  if (l1[0]) drawText(tft, F10, x, 94, l1, mc, C_BG);
  if (l2[0]) drawText(tft, F10, x, 105, l2, mc, C_BG);
  if (!l1[0] && !l2[0]) drawText(tft, F10, x, 94, "NO MESSAGES", C_DIM, C_BG);
  tft.drawFastHLine(x, 116, w, C_PANEL);

  drawText(tft, F10, x, 127, "TEAM RADIO", C_DIM, C_BG);
  if (radioFresh()) {
    tft.fillRect(x, 133, 3, 12, C_TEAL);
    drawText(tft, F16, x + 7, 145, L.radioCode, C_WHITE, C_BG);
    drawTextRight(tft, F10, x + w, 145, "ON AIR", C_TEAL, C_BG);
  } else {
    drawText(tft, F10, x, 145, "STANDBY", C_DIM, C_BG);
  }
}

static void drawFooter() {
  auto& L = f1::live;
  tft.fillRect(0, FOOTER_Y, TFT_W, TFT_H - FOOTER_Y, C_BG);
  tft.fillRect(0, FOOTER_Y, TFT_W, 3, C_PANEL);
  if (L.totalLaps > 0) {
    int w = (int)L.lap * TFT_W / L.totalLaps;
    if (w > TFT_W) w = TFT_W;
    tft.fillRect(0, FOOTER_Y, w, 3, C_TEAL);
  }
  drawText(tft, F10, 6, 166, demoTag ? "DEMO" : "LIVE DATA",
           demoTag ? C_YELLOW : C_DIM, C_BG);
  if (newsOn) {
    const LapNews& n = newsItem();
    bool fastest = n.kind == NEWS_FASTEST;
    uint16_t c = fastest ? C_PURPLE : C_PBEST;
    tft.fillRect(70, 158, 2, 8, c);
    drawText(tft, F10, 77, 166, n.code, C_WHITE, C_BG);
    drawText(tft, F12, 102, 166, n.time, c, C_BG);
    drawText(tft, F10, 158, 166, fastest ? "FASTEST" : "PB", C_DIM, C_BG);
  }
  if (L.totalLaps > 0) {
    char buf[24];
    int left = (int)L.totalLaps - L.lap;
    if (left < 0) left = 0;
    snprintf(buf, sizeof(buf), "%d LAPS TO GO", left);
    drawTextRight(tft, F10, TFT_W - 6, 166, buf, C_SILVER, C_BG);
  }
}

static uint32_t liveSig() {
  auto& L = f1::live;
  uint32_t h = 2166136261u;
  auto mix = [&](uint8_t v) {
    h ^= v;
    h *= 16777619u;
  };
  for (int i = 0; i < L.driverCount; i++) {
    mix((uint8_t)L.pos[i]);
    mix((uint8_t)L.drivers[i].compound);
    mix(L.drivers[i].stintNum);
    for (const char* g = L.drivers[i].gap; *g; g++) mix((uint8_t)*g);
  }
  for (int i = 0; L.rcMsg[i]; i++) mix((uint8_t)L.rcMsg[i]);
  mix(L.lap);
  mix(L.flag);
  mix((uint8_t)(radioFresh() ? 1 : 0));
  mix((uint8_t)(radioFresh() ? L.radioCode[0] : 0));
  mix((uint8_t)(L.track + 0.5f));
  mix((uint8_t)(L.air + 0.5f));
  mix((uint8_t)(L.humidity + 0.5f));
  mix((uint8_t)(L.haveWeather ? 1 : 0));
  mix((uint8_t)(L.havePositions ? 1 : 0));
  mix((uint8_t)(newsOn ? 1 : 0));
  if (newsOn) {
    const LapNews& n = newsItem();
    mix(n.kind);
    for (const char* p = n.code; *p; p++) mix((uint8_t)*p);
    for (const char* p = n.time; *p; p++) mix((uint8_t)*p);
  }
  return h;
}

static void paintLive() {
  drawHeader();
  int slots[ROWS_VIS];
  int c = buildShow(slots);
  for (int i = 0; i < c; i++) {
    int s = slots[i];
    drawRow(LIST_Y + i * ROW_H, f1::live.drivers[s], f1::live.pos[s],
            f1::live.drivers[s].target, i & 1);
  }
  for (int i = c; i < ROWS_VIS; i++)
    tft.fillRect(0, LIST_Y + i * ROW_H, COL_L, ROW_H, C_BG);
  drawPanels();
  drawFooter();
}

void ui::liveFull() {
  tft.fillScreen(C_BG);
  paintLive();
  paintedSession = f1::live.sessionKey;
  livePainted = true;
  lastSig = liveSig();
}

void ui::liveUpdate() {
  if (!livePainted || paintedSession != f1::live.sessionKey) {
    liveFull();
    return;
  }
  newsTick();
  uint32_t sig = liveSig();
  if (sig == lastSig) return;
  paintLive();
  lastSig = sig;
}

// ---------------------------------------------------------------------------
static void drawCountdown() {
  time_t now = time(nullptr);
  long rem = (long)(f1::nextRace.start - now);
  if (rem < 0) rem = 0;
  idleLastRem = rem;

  int d = rem / 86400;
  int h = (rem % 86400) / 3600;
  int m = (rem % 3600) / 60;
  int s = rem % 60;
  if (d > 99) d = 99;

  uint16_t off = rgb(16, 34, 32);
  char days[8];
  snprintf(days, sizeof(days), "%02d", d);
  draw7SegString(tft, 12, 46, 52, days, C_TEAL, off);
  drawText(tft, F12, 84, 98, "DAYS", C_DIM, C_BG);

  char tm[12];
  snprintf(tm, sizeof(tm), "%02d:%02d:%02d", h, m, s);
  draw7SegString(tft, 12, 106, 26, tm, C_TEAL, off);
}

static void idleCountdownPage() {
  tft.fillScreen(C_BG);
  drawStar(tft, STAR20, 4, 0, C_WHITE, C_BG);
  drawText(tft, F12, 27, 15, "NEXT ROUND", C_SILVER, C_BG);

  char clock[8];
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  strftime(clock, sizeof(clock), "%H:%M", &tmv);
  drawTextRight(tft, F12, TFT_W - 4, 15, clock, C_DIM, C_BG);
  tft.drawFastHLine(0, HEADER_H - 1, TFT_W, C_PETRONAS);

  auto& N = f1::nextRace;
  drawText(tft, F10, 12, 40, "LIGHTS OUT IN", C_DIM, C_BG);
  drawCountdown();

  const int x = 150;
  if (N.valid) {
    char buf[48];
    snprintf(buf, sizeof(buf), "ROUND %d", N.round);
    drawText(tft, F10, x, 40, buf, C_DIM, C_BG);

    char name[40];
    upperCopy(name, N.name, sizeof(name));
    char a[48], b[48];
    wrapTwo(name, a, b, sizeof(a), F12, TFT_W - x - 4);
    drawText(tft, F12, x, 60, a, C_SILVER, C_BG);
    if (b[0]) drawText(tft, F12, x, 74, b, C_SILVER, C_BG);

    struct tm rt;
    localtime_r(&N.start, &rt);
    char when[32];
    strftime(when, sizeof(when), "%b %d  %H:%M", &rt);
    upperCopy(buf, when, sizeof(buf));
    drawText(tft, F12, x, 96, buf, C_TEAL, C_BG);
    drawText(tft, F10, x, 109, "LOCAL TIME", C_DIM, C_BG);
  } else {
    drawText(tft, F12, x, 60, "NO RACE DATA", C_SILVER, C_BG);
  }

  tft.drawFastHLine(x, 118, TFT_W - x - 4, C_PANEL);
  drawText(tft, F10, x, 136, "MERCEDES-AMG PETRONAS", C_DIM, C_BG);
  if (demoTag) drawText(tft, F10, 6, 162, "DEMO", C_YELLOW, C_BG);
}

static void idleLastRacePage() {
  tft.fillScreen(C_BG);
  drawStar(tft, STAR20, 4, 0, C_WHITE, C_BG);
  drawText(tft, F12, 27, 15, "LAST RACE", C_SILVER, C_BG);

  auto& L = f1::lastRace;
  char name[40];
  if (L.valid) {
    upperCopy(name, L.name, sizeof(name));
    drawTextRight(tft, F12, TFT_W - 4, 15, name, C_DIM, C_BG);
  }
  tft.drawFastHLine(0, HEADER_H - 1, TFT_W, C_PETRONAS);

  for (int i = 0; i < 5 && L.valid; i++) {
    int y = 28 + i * 22;
    bool target = strstr(L.top[i].team, TARGET_TEAM) != nullptr;
    uint16_t bg = target ? C_TARGET : C_BG;
    if (target) tft.fillRect(0, y - 2, 196, 22, bg);
    char pos[4];
    snprintf(pos, sizeof(pos), "%d", L.top[i].pos);
    drawTextRight(tft, F16, 28, y + 16, pos, i == 0 ? C_TEAL : C_SILVER, bg);
    drawText(tft, F16, 40, y + 16, L.top[i].code, target ? C_WHITE : C_SILVER,
             bg);
    char team[20];
    upperCopy(team, L.top[i].team, sizeof(team));
    drawText(tft, F10, 92, y + 16, team, target ? C_TEAL : C_DIM, bg);
  }
  tft.drawFastVLine(200, 28, 108, C_PANEL);

  auto& N = f1::nextRace;
  const int x = 208;
  drawText(tft, F10, x, 40, "NEXT IN", C_DIM, C_BG);
  if (N.valid) {
    time_t rem = N.start - time(nullptr);
    if (rem < 0) rem = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ldD %02ldH", (long)(rem / 86400),
             (long)((rem % 86400) / 3600));
    drawText(tft, F22, x, 66, buf, C_TEAL, C_BG);

    char nm[40];
    upperCopy(nm, N.name, sizeof(nm));
    char a[48], b[48];
    wrapTwo(nm, a, b, sizeof(a), F10, TFT_W - x - 4);
    drawText(tft, F10, x, 86, a, C_SILVER, C_BG);
    if (b[0]) drawText(tft, F10, x, 98, b, C_SILVER, C_BG);

    struct tm rt;
    localtime_r(&N.start, &rt);
    char when[32];
    strftime(when, sizeof(when), "%b %d  %H:%M", &rt);
    upperCopy(buf, when, sizeof(buf));
    drawText(tft, F10, x, 120, buf, C_DIM, C_BG);
  }

  tft.fillRect(0, TFT_H - 4, TFT_W, 4, C_PETRONAS);
  drawText(tft, F10, 6, 162, "MERCEDES-AMG PETRONAS", C_DIM, C_BG);
  if (demoTag) drawTextRight(tft, F10, TFT_W - 6, 162, "DEMO", C_YELLOW, C_BG);
}

void ui::idleShow(bool countdown) {
  idleCountdown = countdown;
  if (countdown)
    idleCountdownPage();
  else
    idleLastRacePage();
}

void ui::idleTick() {
  if (!idleCountdown || !f1::nextRace.valid) return;
  long rem = (long)(f1::nextRace.start - time(nullptr));
  if (rem < 0) rem = 0;
  if (rem == idleLastRem) return;

  tft.fillRect(0, 44, 148, 96, C_BG);
  drawCountdown();
}
