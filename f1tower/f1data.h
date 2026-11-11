#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

enum FlagState : uint8_t {
  FLAG_GREEN = 0,
  FLAG_YELLOW,
  FLAG_SC,
  FLAG_VSC,
  FLAG_RED,
  FLAG_CHEQUERED,
};

struct DriverInfo {
  uint8_t num = 0;
  char code[4] = "";
  char last[14] = "";
  char team[22] = "";
  char gap[12] = "";  // "+2.431", "+1 LAP" or "LEADER"; empty when unknown
  bool gapLeader = false;
  uint16_t colour = 0x7BEF;
  bool target = false;
  char compound = 0;
  uint8_t lapStart = 0;
  uint8_t stintNum = 0;
  float bestLap = 0;  // fastest clean lap of this driver, seconds
};

enum LapNewsKind : uint8_t { NEWS_NONE = 0, NEWS_FASTEST, NEWS_PB };

struct LapNews {
  uint16_t seq = 0;
  uint8_t kind = NEWS_NONE;
  char code[4] = "";
  char time[10] = "";
};

struct LiveState {
  bool haveSession = false;
  bool live = false;
  long sessionKey = 0;
  uint16_t circuitKey = 0;
  char circuit[24] = "";
  char sessionName[14] = "";
  time_t startUtc = 0;
  time_t endUtc = 0;

  DriverInfo drivers[24];
  uint8_t driverCount = 0;
  int8_t pos[24];
  bool havePositions = false;

  uint8_t lap = 0;
  uint8_t totalLaps = 0;

  bool haveWeather = false;
  float air = 0, track = 0, humidity = 0;
  bool rain = false;

  uint8_t flag = FLAG_GREEN;
  char rcMsg[56] = "";

  char radioCode[5] = "";
  uint32_t radioAtMs = 0;

  float lapBest = 0;  // fastest lap of the session, seconds
  LapNews news[NEWS_SLOTS];
  uint16_t newsSeq = 0;  // seq of the newest queued item

  uint32_t lastOkMs = 0;
};

struct NextRace {
  bool valid = false;
  uint8_t round = 0;
  char name[40] = "";
  char locality[24] = "";
  time_t start = 0;
};

struct LastRace {
  bool valid = false;
  char name[40] = "";
  struct Row {
    uint8_t pos;
    char code[4];
    char team[20];
  } top[5];
};

namespace f1 {
extern LiveState live;
extern NextRace nextRace;
extern LastRace lastRace;

bool fetchSession(bool& isLive, bool& changed);
bool fetchDrivers();
bool fetchPositions();
bool fetchIntervals();
bool fetchWeather();
bool fetchRaceControl();
bool fetchRadio();
bool fetchLaps();
bool lapsSeeded();
bool fetchStints();
bool fetchNextRace();
bool fetchLastRace();

void resetSession();
int slotOfNum(int num);
uint8_t totalLapsFor(uint16_t key);
time_t isoToEpoch(const char* s);
void loadDemo();
}  // namespace f1
