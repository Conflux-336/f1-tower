#pragma once
#include <stdint.h>


constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

// ---- identity -------------------------------------------------------------
#define TARGET_TEAM       "Mercedes"
#define TARGET_TEAM_SHORT "MER"

// ---- panel (landscape) ----------------------------------------------------
#define TFT_ROTATION 1
#define TFT_W 320
#define TFT_H 170
#define BACKLIGHT_PIN 14
#define BACKLIGHT_LEVEL 150
#define BACKLIGHT_DIM 90
#define BACKLIGHT_NIGHT 40

// ---- input ----------------------------------------------------------------
#define BUTTON_PIN 0  // BOOT key, active low
#define BUTTON_TAP_MS 40
#define BUTTON_HOLD_MS 900
#define BUTTON_DBL_MS 400

// ---- polling (ms) ---------------------------------------------------------
#define SESSION_POLL_MS   30000
#define POSITION_POLL_MS   2000
#define INTERVAL_POLL_MS   6000
#define WEATHER_POLL_MS   30000
#define RACE_CTRL_POLL_MS  8000
#define RADIO_POLL_MS     10000
#define LAPS_POLL_MS       6000
#define STINT_POLL_MS     30000
#define META_POLL_MS     300000

// ---- fastest / personal best ticker ---------------------------------------
#define NEWS_SLOTS        6
#define NEWS_HOLD_MS   4000
#define LAP_SEED_CHUNK    8  // laps per backfill request (~70 KB each)

// ---- layout ---------------------------------------------------------------
#define HEADER_H   21
#define STRIP_H    16
#define ROW_H      16
#define ROWS_VIS    8
#define LIST_Y     24
#define COL_L     176
#define BODY_Y     24
#define FOOTER_Y  152

