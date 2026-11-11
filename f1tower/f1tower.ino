#include <Arduino.h>
#include "config.h"
#include "theme.h"
#include "net.h"
#include "f1data.h"
#include "ui.h"
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();

enum Mode { M_BOOT, M_IDLE, M_LIVE };

static Mode mode = M_BOOT;
static bool demo = false;
static bool idlePage = true;

static uint32_t tSession, tPos, tGap, tWx, tRc, tRadio, tLaps, tStint, tMeta,
    tTick, tReconnect, tHeap;

static bool due(uint32_t& last, uint32_t interval) {
  uint32_t now = millis();
  if (now - last >= interval) {
    last = now;
    return true;
  }
  return false;
}

// ---- button ---------------------------------------------------------------
// Tap: step the backlight through day/dim/night. Double tap: next idle page.
// Hold: poll everything again (and retry the link if it is down) without
// waiting for the poll timers.
#define BTN_NONE 0
#define BTN_TAP 1
#define BTN_HOLD 2
#define BTN_DOUBLE 3

static const uint8_t kBacklightSteps[] = {BACKLIGHT_LEVEL, BACKLIGHT_DIM,
                                          BACKLIGHT_NIGHT};
static uint8_t backlightStep = 0;

// A tap is held back for BUTTON_DBL_MS so a second one can be recognised;
// without that delay every double tap would also change the backlight. The
// pair is decided when the second press goes down, so a slow second click
// still counts as a double instead of collapsing into two backlight steps.
static uint8_t buttonPoll() {
  static bool down = false, holdFired = false, tapPending = false,
              dblArmed = false;
  static uint32_t tDown = 0, tTap = 0;
  bool pressed = digitalRead(BUTTON_PIN) == LOW;
  uint32_t ms = millis();

  if (pressed && !down) {
    down = true;
    holdFired = false;
    dblArmed = tapPending && ms - tTap <= BUTTON_DBL_MS;
    tapPending = false;
    tDown = ms;
  } else if (pressed && down) {
    if (!holdFired && ms - tDown >= BUTTON_HOLD_MS) {
      holdFired = true;
      dblArmed = false;
      return BTN_HOLD;
    }
  } else if (down) {
    down = false;
    if (holdFired || ms - tDown < BUTTON_TAP_MS) return BTN_NONE;
    if (dblArmed) {
      dblArmed = false;
      return BTN_DOUBLE;
    }
    tapPending = true;
    tTap = ms;
  }
  if (tapPending && ms - tTap > BUTTON_DBL_MS) {
    tapPending = false;
    return BTN_TAP;
  }
  return BTN_NONE;
}

static void buttonAction(uint8_t ev) {
  if (ev == BTN_TAP) {
    backlightStep = (backlightStep + 1) % 3;
    ui::backlight(kBacklightSteps[backlightStep]);
    Serial.printf("[btn] backlight %u\n", kBacklightSteps[backlightStep]);
  } else if (ev == BTN_DOUBLE) {
    if (mode == M_IDLE) {
      idlePage = !idlePage;
      ui::idleShow(idlePage);
      Serial.printf("[btn] page %s\n", idlePage ? "countdown" : "last race");
    } else if (mode == M_LIVE) {
      Serial.println("[btn] page (live is the only screen)");
    }
  } else if (ev == BTN_HOLD) {
    uint32_t now = millis();
    tSession = tPos = tGap = tWx = tRc = tRadio = tLaps = tStint = tMeta =
        now - 600000;
    tReconnect = now - 600000;
    // Repaint so the press has a visible ack even when the data is unchanged.
    if (mode == M_LIVE) ui::liveFull();
    else if (mode == M_IDLE) ui::idleShow(idlePage);
    Serial.println("[btn] refresh now");
  }
}

static void resetTimers() {
  uint32_t now = millis();
  tPos = tGap = tWx = tRc = tRadio = tLaps = tStint = now;
}

static void enterLive() {
  Serial.println("[mode] live");
  mode = M_LIVE;
  if (!demo) {
    f1::fetchDrivers();
    f1::fetchPositions();
    f1::fetchIntervals();
    f1::fetchStints();
    f1::fetchWeather();
    f1::fetchRaceControl();
    f1::fetchRadio();
    f1::fetchLaps();
  }
  ui::liveFull();
  resetTimers();
  Serial.printf("[heap] %u\n", ESP.getFreeHeap());
}

static void enterIdle() {
  Serial.println("[mode] idle");
  mode = M_IDLE;
  idlePage = true;
  ui::idleShow(true);
  tMeta = millis();
  tTick = millis() - 1000;
}

static void setupDemo() {
  demo = true;
  ui::setDemo(true);
  f1::loadDemo();
  enterLive();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[boot] f1 tower");

  ui::init();
  ui::backlight(BACKLIGHT_LEVEL);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  ui::splash("LIVE TIMING", "MERCEDES-AMG PETRONAS");
  delay(1600);

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[boot] no wifi credentials -> demo");
    setupDemo();
    return;
  }

  ui::splash("CONNECTING", "WIFI + NTP");
  if (!net::connect()) {
    ui::error("WIFI FAILED", WIFI_SSID);
    delay(2500);
    setupDemo();
    return;
  }

  ui::splash("SYNCING", "OPENF1 + JOLPICA");
  f1::fetchNextRace();
  f1::fetchLastRace();

  bool isLive = false, changed = false;
  if (f1::fetchSession(isLive, changed) && isLive) {
    enterLive();
  } else {
    enterIdle();
  }

  Serial.printf("[heap] %u min %u\n", ESP.getFreeHeap(),
                ESP.getMinFreeHeap());
}

static void demoTick() {
  static uint32_t tSwap = 0, tLive = 0;
  static uint8_t page = 0;
  if (millis() - tSwap > 8000) {
    tSwap = millis();
    page = (page + 1) % 3;
    if (page == 0) {
      mode = M_LIVE;
      ui::liveFull();
    } else {
      mode = M_IDLE;
      idlePage = page == 1;
      ui::idleShow(idlePage);
    }
  }
  if (mode == M_LIVE && millis() - tLive > 1500) {
    tLive = millis();
    ui::liveUpdate();
  }
  if (mode == M_IDLE && millis() - tLive > 1000) {
    tLive = millis();
    ui::idleTick();
  }
  delay(20);
}

static void liveTick() {
  if (due(tSession, SESSION_POLL_MS)) {
    bool isLive = false, changed = false;
    if (f1::fetchSession(isLive, changed)) {
      if (changed) {
        f1::fetchDrivers();
        f1::fetchPositions();
        f1::fetchIntervals();
        f1::fetchStints();
        ui::liveFull();
      }
      if (!isLive) {
        f1::fetchNextRace();
        f1::fetchLastRace();
        enterIdle();
        return;
      }
    }
  }
  if (due(tPos, POSITION_POLL_MS)) {
    f1::fetchPositions();
    ui::liveUpdate();
  }
  if (due(tGap, INTERVAL_POLL_MS)) {
    f1::fetchIntervals();
    ui::liveUpdate();
  }
  if (due(tWx, WEATHER_POLL_MS)) {
    f1::fetchWeather();
    ui::liveUpdate();
  }
  if (due(tRc, RACE_CTRL_POLL_MS)) {
    f1::fetchRaceControl();
    ui::liveUpdate();
  }
  if (due(tRadio, RADIO_POLL_MS)) {
    f1::fetchRadio();
    ui::liveUpdate();
  }
  if (due(tLaps, f1::lapsSeeded() ? LAPS_POLL_MS : 250)) {
    f1::fetchLaps();
    ui::liveUpdate();
  }
  if (due(tStint, STINT_POLL_MS)) {
    f1::fetchStints();
    ui::liveUpdate();
  }
  ui::liveUpdate();
}

static void idleTick() {
  if (due(tSession, SESSION_POLL_MS)) {
    bool isLive = false, changed = false;
    if (f1::fetchSession(isLive, changed) && isLive) {
      enterLive();
      return;
    }
  }
  if (due(tMeta, META_POLL_MS)) {
    f1::fetchNextRace();
    f1::fetchLastRace();
    ui::idleShow(idlePage);
  }
  if (idlePage) ui::idleTick();
}

void loop() {
  buttonAction(buttonPoll());

  if (demo) {
    if (strlen(WIFI_SSID) && millis() - tReconnect > 180000) {
      tReconnect = millis();
      if (net::connect(8000)) {
        demo = false;
        ui::setDemo(false);
        Serial.println("[net] recovered -> real data");
        ui::splash("SYNCING", "OPENF1 + JOLPICA");
        f1::fetchNextRace();
        f1::fetchLastRace();
        bool isLive = false, changed = false;
        if (f1::fetchSession(isLive, changed) && isLive) {
          enterLive();
        } else {
          enterIdle();
        }
      }
    }
    demoTick();
    return;
  }

  if (!net::online()) {
    if (millis() - tReconnect > 15000) {
      tReconnect = millis();
      Serial.println("[net] reconnecting");
      if (net::connect(12000)) {
        tSession = 0;
        tMeta = 0;
      }
    }
  } else if (mode == M_LIVE) {
    liveTick();
  } else if (mode == M_IDLE) {
    idleTick();
  }

  if (millis() - tHeap > 60000) {
    tHeap = millis();
    Serial.printf("[heap] %u min %u psram %u\n", ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), ESP.getFreePsram());
  }
  delay(20);
}
