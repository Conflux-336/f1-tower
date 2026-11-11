#include "f1data.h"
#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "laps_table.h"
#include "net.h"
#include "jsonstream.h"

namespace f1 {

LiveState live;
NextRace nextRace;
LastRace lastRace;

static const char* API = "https://api.openf1.org/v1";
static const char* JOLPICA = "https://api.jolpi.ca/ergast/f1";

static char posCursor[32] = "";
static char gapCursor[32] = "";
static char wxCursor[32] = "";
static char rcCursor[32] = "";
static char radioCursor[32] = "";
static uint32_t radioStamp = 0;
static bool lapSeeded = false;
static uint8_t lapSeed = 0;  // next lap to backfill; 0 = bootstrap not done

static void epochToIso(time_t t, char* out, size_t n) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, n, "%Y-%m-%dT%H:%M:%S", &tmv);
}

static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

time_t isoToEpoch(const char* s) {
  if (!s || !*s) return 0;
  int Y, M, D, h, mi, sec;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &sec) != 6) return 0;
  return (time_t)(daysFromCivil(Y, (unsigned)M, (unsigned)D) * 86400 +
                  h * 3600 + mi * 60 + sec);
}

static void urlEncode(const char* in, char* out, size_t n) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (const char* p = in; *p && o + 3 < n; p++) {
    uint8_t c = (uint8_t)*p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0x0F];
    }
  }
  out[o] = 0;
}

static String cursorParam(const char* cursor) {
  time_t t = f1::isoToEpoch(cursor);
  if (t <= 0) return "";
  char iso[24], enc[96];
  epochToIso(t - 1, iso, sizeof(iso));
  urlEncode(iso, enc, sizeof(enc));
  return String("&date%3E%3D") + enc;
}

static bool getJson(const String& url, JsonDocument& doc,
                    uint32_t timeout = 15000) {
  net::Body body;
  int code = net::getBody(url, body, timeout);
  if (code != 200) {
    Serial.printf("[api] %s -> %d\n", url.c_str(), code);
    return false;
  }
  DeserializationError err = deserializeJson(doc, body.data, body.len);
  if (err) {
    Serial.printf("[api] %s json %s (%u bytes)\n", url.c_str(), err.c_str(),
                  (unsigned)body.len);
    return false;
  }
  f1::live.lastOkMs = millis();
  return true;
}

static bool getFlat(const String& url, net::Body& body, uint32_t timeout) {
  int code = net::getBody(url, body, timeout);
  if (code == 404) return true;  // OpenF1 answers 404 when a date filter matches nothing
  if (code != 200) {
    Serial.printf("[api] %s -> %d\n", url.c_str(), code);
    return false;
  }
  f1::live.lastOkMs = millis();
  return true;
}

static uint16_t hexColour(const char* s) {
  if (!s || strlen(s) < 6) return 0x7BEF;
  uint32_t v = strtoul(s, nullptr, 16);
  return rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

int slotOfNum(int num) {
  for (uint8_t i = 0; i < live.driverCount; i++)
    if (live.drivers[i].num == num) return i;
  return -1;
}

uint8_t totalLapsFor(uint16_t key) {
  for (size_t i = 0; i < sizeof(CIRCUIT_LAPS) / sizeof(CIRCUIT_LAPS[0]); i++) {
    if (CIRCUIT_LAPS[i].key == key) return CIRCUIT_LAPS[i].laps;
  }
  return 0;
}

void resetSession() {
  long keep = live.sessionKey;
  live = LiveState();
  live.sessionKey = keep;
  posCursor[0] = wxCursor[0] = rcCursor[0] = radioCursor[0] = 0;
  gapCursor[0] = 0;
  radioStamp = 0;
  lapSeeded = false;
  lapSeed = 0;
}

bool fetchSession(bool& isLive, bool& changed) {
  isLive = false;
  changed = false;
  JsonDocument doc;
  if (!getJson(String(API) + "/sessions?session_key=latest", doc)) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return false;
  JsonObject o = arr[0];

  long key = o["session_key"] | 0L;
  time_t st = isoToEpoch(o["date_start"] | "");
  time_t en = isoToEpoch(o["date_end"] | "");
  const char* cname = o["circuit_short_name"] | "";
  const char* sname = o["session_name"] | "";

  if (key != live.sessionKey) {
    resetSession();
    live.sessionKey = key;
    live.circuitKey = (uint16_t)(o["circuit_key"] | 0);
    strncpy(live.circuit, cname, sizeof(live.circuit) - 1);
    strncpy(live.sessionName, sname, sizeof(live.sessionName) - 1);
    live.startUtc = st;
    live.endUtc = en;
    live.haveSession = true;
    live.totalLaps = totalLapsFor(live.circuitKey);
    changed = true;
  }

  time_t now = time(nullptr);
  bool window = st > 0 && now >= st - 300 && now <= en + 1200;
  live.live = window;
  isLive = window;
  Serial.printf("[api] session %ld %s start %ld end %ld live %d\n", key, cname,
                (long)st, (long)en, (int)window);
  return true;
}

bool fetchDrivers() {
  if (!live.haveSession) return false;
  JsonDocument doc;
  String url = String(API) + "/drivers?session_key=" + String(live.sessionKey);
  if (!getJson(url, doc)) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  live.driverCount = 0;
  for (JsonObject o : arr) {
    if (live.driverCount >= 24) break;
    DriverInfo& d = live.drivers[live.driverCount];
    d = DriverInfo();
    d.num = o["driver_number"] | 0;
    strncpy(d.code, o["name_acronym"] | "???", sizeof(d.code) - 1);
    const char* full = o["full_name"] | "";
    const char* sp = strrchr(full, ' ');
    const char* ln = (sp && sp[1]) ? sp + 1 : full;
    size_t i = 0;
    for (; ln[i] && i + 1 < sizeof(d.last); i++)
      d.last[i] = (ln[i] >= 'a' && ln[i] <= 'z') ? ln[i] - 32 : ln[i];
    d.last[i] = 0;
    strncpy(d.team, o["team_name"] | "", sizeof(d.team) - 1);
    d.colour = hexColour(o["team_colour"] | "");
    d.target = strncasecmp(d.team, TARGET_TEAM, strlen(TARGET_TEAM)) == 0;
    live.pos[live.driverCount] = -1;
    live.driverCount++;
  }
  Serial.printf("[api] %u drivers loaded\n", live.driverCount);
  return live.driverCount > 0;
}

namespace {

struct PosSink : FlatSink {
  const char* cursor;
  char maxDate[32] = "";
  int num = 0, pos = 0;
  char date[32] = "";
  int applied = 0;
  void startObject() override {
    num = pos = 0;
    date[0] = 0;
  }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "driver_number")) num = atoi(v);
    else if (!strcmp(k, "position")) pos = atoi(v);
    else if (!strcmp(k, "date")) {
      strncpy(date, v, sizeof(date) - 1);
      date[sizeof(date) - 1] = 0;
    }
  }
  void endObject() override {
    if (num <= 0 || pos <= 0 || !date[0]) return;
    if (cursor && cursor[0] && strcmp(date, cursor) < 0) return;
    int slot = f1::slotOfNum(num);
    if (slot >= 0) {
      f1::live.pos[slot] = (int8_t)pos;
      applied++;
    }
    if (strcmp(date, maxDate) > 0) strcpy(maxDate, date);
  }
};

struct WeatherSink : FlatSink {
  float air = 0, track = 0, hum = 0;
  bool rain = false;
  bool seen = false;
  char date[32] = "";
  char maxDate[32] = "";
  void startObject() override { date[0] = 0; }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "air_temperature")) air = atof(v);
    else if (!strcmp(k, "track_temperature")) track = atof(v);
    else if (!strcmp(k, "humidity")) hum = atof(v);
    else if (!strcmp(k, "rainfall")) rain = atoi(v) != 0;
    else if (!strcmp(k, "date")) {
      strncpy(date, v, sizeof(date) - 1);
      date[sizeof(date) - 1] = 0;
    }
  }
  void endObject() override {
    seen = true;
    if (strcmp(date, maxDate) > 0) strcpy(maxDate, date);
  }
};

struct RadioSink : FlatSink {
  int num = 0;
  char date[32] = "";
  int bestNum = 0;
  char bestDate[32] = "";
  void startObject() override {
    num = 0;
    date[0] = 0;
  }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "driver_number")) num = atoi(v);
    else if (!strcmp(k, "date")) {
      strncpy(date, v, sizeof(date) - 1);
      date[sizeof(date) - 1] = 0;
    }
  }
  void endObject() override {
    if (num <= 0 || !date[0]) return;
    if (strcmp(date, bestDate) > 0) {
      strcpy(bestDate, date);
      bestNum = num;
    }
  }
};

struct RcSink : FlatSink {
  char date[32] = "";
  char category[24] = "";
  char flag[16] = "";
  char msg[160] = "";
  char bestDate[32] = "";
  int bestNum = 0;
  char bestCat[24] = "";
  char bestFlag[16] = "";
  char bestMsg[56] = "";
  void startObject() override {
    date[0] = category[0] = flag[0] = msg[0] = 0;
  }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "date")) {
      strncpy(date, v, sizeof(date) - 1);
      date[sizeof(date) - 1] = 0;
    } else if (!strcmp(k, "category")) {
      strncpy(category, v, sizeof(category) - 1);
    } else if (!strcmp(k, "flag")) {
      strncpy(flag, v, sizeof(flag) - 1);
    } else if (!strcmp(k, "message")) {
      strncpy(msg, v, sizeof(msg) - 1);
      msg[sizeof(msg) - 1] = 0;
    }
  }
  void endObject() override {
    if (!date[0]) return;
    if (strcmp(date, bestDate) < 0) return;
    strcpy(bestDate, date);
    strncpy(bestCat, category, sizeof(bestCat) - 1);
    strncpy(bestFlag, flag, sizeof(bestFlag) - 1);
    strncpy(bestMsg, msg, sizeof(bestMsg) - 1);
    bestMsg[sizeof(bestMsg) - 1] = 0;
  }
};

static void formatLap(char* out, size_t n, float secs) {
  int ms = (int)(secs * 1000.0f + 0.5f);
  if (ms >= 60000)
    snprintf(out, n, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
  else
    snprintf(out, n, "%d.%03d", ms / 1000, ms % 1000);
}

static void pushNews(uint8_t kind, const char* code, float secs) {
  live.newsSeq++;
  LapNews& n = live.news[live.newsSeq % NEWS_SLOTS];
  n.seq = live.newsSeq;
  n.kind = kind;
  strncpy(n.code, code, sizeof(n.code) - 1);
  n.code[sizeof(n.code) - 1] = 0;
  formatLap(n.time, sizeof(n.time), secs);
  Serial.printf("[lap] %s %s %s\n", kind == NEWS_FASTEST ? "fastest" : "pb",
                n.code, n.time);
}

static void noteLap(int num, float secs, bool quiet) {
  int slot = slotOfNum(num);
  if (slot < 0) return;
  DriverInfo& d = live.drivers[slot];
  if (d.bestLap > 0 && secs >= d.bestLap - 0.0005f) return;
  d.bestLap = secs;
  bool fastest = live.lapBest <= 0 || secs < live.lapBest - 0.0005f;
  if (fastest) live.lapBest = secs;
  if (!quiet) pushNews(fastest ? NEWS_FASTEST : NEWS_PB, d.code, secs);
}

struct LapsSink : FlatSink {
  bool quiet = false;  // backfill: learn bests without flashing anything
  int lap = 0, maxLap = 0, num = 0;
  float dur = 0;
  bool pitOut = false;
  void startObject() override {
    lap = num = 0;
    dur = 0;
    pitOut = false;
  }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "lap_number")) lap = atoi(v);
    else if (!strcmp(k, "driver_number")) num = atoi(v);
    else if (!strcmp(k, "lap_duration")) dur = atof(v);  // "null" -> 0
    else if (!strcmp(k, "is_pit_out_lap")) pitOut = !strcmp(v, "true");
  }
  void endObject() override {
    if (lap > maxLap) maxLap = lap;
    // Out-laps are slow by design and would poison a driver's baseline.
    if (num <= 0 || dur < 30.0f || pitOut) return;
    noteLap(num, dur, quiet);
  }
};

static bool readLaps(const String& url, bool quiet) {
  net::Body body;
  if (!getFlat(url, body, 20000)) return false;
  LapsSink sink;
  sink.quiet = quiet;
  bool ok = jsonstream::readArray(body.data, body.len, sink);
  if (sink.maxLap > live.lap) live.lap = (uint8_t)sink.maxLap;
  if (!ok)
    Serial.printf("[api] laps truncated (%u bytes)\n", (unsigned)body.len);
  return ok;
}

struct StintSink : FlatSink {
  int num = 0, stint = 0, lapStart = 0;
  char compound[12] = "";
  void startObject() override {
    num = stint = lapStart = 0;
    compound[0] = 0;
  }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "driver_number")) num = atoi(v);
    else if (!strcmp(k, "stint_number")) stint = atoi(v);
    else if (!strcmp(k, "lap_start")) lapStart = atoi(v);
    else if (!strcmp(k, "compound")) {
      strncpy(compound, v, sizeof(compound) - 1);
      compound[sizeof(compound) - 1] = 0;
    }
  }
  void endObject() override {
    if (num <= 0 || !compound[0]) return;
    int slot = f1::slotOfNum(num);
    if (slot < 0) return;
    DriverInfo& d = f1::live.drivers[slot];
    if (stint >= d.stintNum) {
      d.stintNum = (uint8_t)stint;
      d.compound = compound[0];
      d.lapStart = (uint8_t)lapStart;
    }
  }
};

struct GapSink : FlatSink {
  const char* cursor;
  char maxDate[32] = "";
  int num = 0;
  float secs = 0;
  bool haveNum = false;
  char text[12] = "";
  char date[32] = "";
  int applied = 0;
  void startObject() override {
    num = 0;
    secs = 0;
    haveNum = false;
    text[0] = date[0] = 0;
  }
  void field(const char* k, const char* v) override {
    if (!strcmp(k, "driver_number")) {
      num = atoi(v);
    } else if (!strcmp(k, "gap_to_leader")) {
      // Number for running cars, "+1 LAP" once lapped, null when unknown.
      if (!strcmp(v, "null")) return;
      if (v[0] == '+' || (v[0] >= 'A' && v[0] <= 'Z')) {
        strncpy(text, v, sizeof(text) - 1);
        text[sizeof(text) - 1] = 0;
      } else {
        secs = atof(v);
        haveNum = true;
      }
    } else if (!strcmp(k, "date")) {
      strncpy(date, v, sizeof(date) - 1);
      date[sizeof(date) - 1] = 0;
    }
  }
  void endObject() override {
    if (num <= 0 || !date[0]) return;
    if (cursor && cursor[0] && strcmp(date, cursor) < 0) return;
    int slot = f1::slotOfNum(num);
    if (slot < 0) return;
    DriverInfo& d = f1::live.drivers[slot];
    if (haveNum) {
      d.gapLeader = secs <= 0.0005f;
      if (d.gapLeader)
        strcpy(d.gap, "LEADER");
      else
        snprintf(d.gap, sizeof(d.gap), "+%.3f", secs);
    } else if (text[0]) {
      strncpy(d.gap, text, sizeof(d.gap) - 1);
      d.gap[sizeof(d.gap) - 1] = 0;
      d.gapLeader = false;
    } else {
      return;
    }
    applied++;
    if (strcmp(date, maxDate) > 0) strcpy(maxDate, date);
  }
};

}  // namespace

bool fetchIntervals() {
  if (!live.haveSession) return false;
  // The endpoint replays the whole session (several MB), so the first call
  // starts 30 s back and every later one continues from the newest date seen.
  if (!gapCursor[0]) {
    char iso[24];
    epochToIso(time(nullptr) - 30, iso, sizeof(iso));
    strcpy(gapCursor, iso);
  }
  String url = String(API) + "/intervals?session_key=" +
               String(live.sessionKey) + cursorParam(gapCursor);

  net::Body body;
  if (!getFlat(url, body, 15000)) return false;
  GapSink sink;
  sink.cursor = gapCursor;
  bool ok = jsonstream::readArray(body.data, body.len, sink);
  if (sink.maxDate[0]) strcpy(gapCursor, sink.maxDate);
  if (!ok)
    Serial.printf("[api] gaps truncated after %d rows (%u bytes)\n",
                  sink.applied, (unsigned)body.len);
  return ok;
}

bool fetchPositions() {
  if (!live.haveSession) return false;
  String url = String(API) + "/position?session_key=" + String(live.sessionKey);
  if (posCursor[0]) url += cursorParam(posCursor);

  net::Body body;
  if (!getFlat(url, body, posCursor[0] ? 15000 : 35000)) return false;
  PosSink sink;
  sink.cursor = posCursor;
  bool ok = jsonstream::readArray(body.data, body.len, sink);
  if (sink.maxDate[0]) strcpy(posCursor, sink.maxDate);
  if (sink.applied) live.havePositions = true;
  if (!ok)
    Serial.printf("[api] pos truncated after %d rows (%u bytes)\n",
                  sink.applied, (unsigned)body.len);
  return ok;
}

bool fetchWeather() {
  if (!live.haveSession) return false;
  String url = String(API) + "/weather?session_key=" + String(live.sessionKey);
  if (wxCursor[0]) url += cursorParam(wxCursor);
  net::Body body;
  if (!getFlat(url, body, 15000)) return false;
  WeatherSink sink;
  bool ok = jsonstream::readArray(body.data, body.len, sink);
  if (sink.seen) {
    live.air = sink.air;
    live.track = sink.track;
    live.humidity = sink.hum;
    live.rain = sink.rain;
    live.haveWeather = true;
    if (sink.maxDate[0]) {
      strcpy(wxCursor, sink.maxDate);
    } else {
      time_t t = time(nullptr) - 5;
      char iso[24];
      epochToIso(t, iso, sizeof(iso));
      strcpy(wxCursor, iso);
    }
  }
  return ok;
}

bool fetchRadio() {
  if (!live.haveSession) return false;
  String url = String(API) + "/team_radio?session_key=" + String(live.sessionKey);
  if (radioCursor[0]) url += cursorParam(radioCursor);
  net::Body body;
  if (!getFlat(url, body, 15000)) return false;
  RadioSink sink;
  bool ok = jsonstream::readArray(body.data, body.len, sink);
  if (sink.bestDate[0]) {
    bool fresh = strcmp(radioCursor, sink.bestDate) != 0;
    strcpy(radioCursor, sink.bestDate);
    int slot = slotOfNum(sink.bestNum);
    if (fresh && slot >= 0) {
      strncpy(live.radioCode, live.drivers[slot].code, sizeof(live.radioCode) - 1);
      radioStamp = millis();
      live.radioAtMs = radioStamp;
      Serial.printf("[api] radio %s\n", live.radioCode);
    }
  }
  return ok;
}

bool fetchRaceControl() {
  if (!live.haveSession) return false;
  String url =
      String(API) + "/race_control?session_key=" + String(live.sessionKey);
  if (rcCursor[0]) url += cursorParam(rcCursor);
  net::Body body;
  if (!getFlat(url, body, 15000)) return false;
  RcSink sink;
  bool ok = jsonstream::readArray(body.data, body.len, sink);
  if (!sink.bestDate[0]) return ok;

  strcpy(rcCursor, sink.bestDate);

  if (!strcmp(sink.bestCat, "Flag")) {
    if (!strcmp(sink.bestFlag, "RED")) live.flag = FLAG_RED;
    else if (!strcmp(sink.bestFlag, "YELLOW")) live.flag = FLAG_YELLOW;
    else if (!strcmp(sink.bestFlag, "CHEQUERED")) live.flag = FLAG_CHEQUERED;
    else if (!strcmp(sink.bestFlag, "GREEN") || !strcmp(sink.bestFlag, "CLEAR"))
      live.flag = FLAG_GREEN;
  }
  if (strstr(sink.bestMsg, "VIRTUAL SAFETY CAR")) live.flag = FLAG_VSC;
  else if (strstr(sink.bestMsg, "SAFETY CAR")) {
    if (strstr(sink.bestMsg, "IN THIS LAP")) live.flag = FLAG_GREEN;
    else live.flag = FLAG_SC;
  }
  if (strstr(sink.bestMsg, "RED FLAG")) live.flag = FLAG_RED;
  if (strstr(sink.bestMsg, "CLEAR")) live.flag = FLAG_GREEN;
  strncpy(live.rcMsg, sink.bestMsg, sizeof(live.rcMsg) - 1);
  return ok;
}

bool lapsSeeded() { return lapSeeded; }

// A panel that wakes up mid-race still needs the earlier laps: the fastest-lap
// marker only means something against the whole session, and a driver's
// personal best may have been set before boot. The backfill walks the session
// in chunks of LAP_SEED_CHUNK laps (one per call) so no response gets near the
// body cap, and stays silent so the ticker never replays old laps.
static bool seedLaps() {
  if (lapSeed == 0) {
    int leader = -1;
    for (uint8_t i = 0; i < live.driverCount; i++)
      if (live.pos[i] == 1) leader = i;
    if (leader < 0) return false;

    String url = String(API) + "/laps?session_key=" + String(live.sessionKey) +
                 "&driver_number=" + String(live.drivers[leader].num);
    if (!readLaps(url, true)) return false;
    lapSeed = 1;
    Serial.printf("[api] laps %u, backfill from 1\n", live.lap);
    return true;
  }

  if (live.lap == 0 || lapSeed > live.lap) {
    lapSeeded = true;
    Serial.printf("[api] lap bests ready, fastest %.3f\n", live.lapBest);
    return true;
  }

  int to = (int)lapSeed + LAP_SEED_CHUNK - 1;
  if (to > (int)live.lap) to = live.lap;
  String url = String(API) + "/laps?session_key=" + String(live.sessionKey) +
               "&lap_number%3E%3D" + String(lapSeed) + "&lap_number%3C%3D" +
               String(to);
  if (!readLaps(url, true)) return false;
  lapSeed = (uint8_t)(to + 1);
  return true;
}

bool fetchLaps() {
  if (!live.haveSession) return false;
  if (!lapSeeded) return seedLaps();

  String url = String(API) + "/laps?session_key=" + String(live.sessionKey);
  // Overlap the last few laps so a late row still lands, and so the very first
  // call of a race (lap 1-3) simply asks for the whole session.
  int from = (int)live.lap - 3;
  if (from > 1) url += "&lap_number%3E%3D" + String(from);
  return readLaps(url, false);
}

bool fetchStints() {
  if (!live.haveSession) return false;
  String url = String(API) + "/stints?session_key=" + String(live.sessionKey);
  net::Body body;
  if (!getFlat(url, body, 15000)) return false;
  StintSink sink;
  return jsonstream::readArray(body.data, body.len, sink);
}

bool fetchNextRace() {
  JsonDocument doc;
  if (!getJson(String(JOLPICA) + "/current/next.json", doc)) return false;
  JsonObject r = doc["MRData"]["RaceTable"]["Races"][0];
  if (r.isNull()) return false;

  NextRace n;
  n.valid = true;
  n.round = atoi(r["round"] | "0");
  strncpy(n.name, r["raceName"] | "", sizeof(n.name) - 1);
  strncpy(n.locality, r["Circuit"]["Location"]["locality"] | "",
          sizeof(n.locality) - 1);
  String date = r["date"] | "";
  String tm = r["time"] | "12:00:00Z";
  n.start = isoToEpoch((date + "T" + tm.substring(0, 8)).c_str());
  nextRace = n;
  Serial.printf("[api] next R%u %s %ld\n", n.round, n.name, (long)n.start);
  return true;
}

bool fetchLastRace() {
  JsonDocument doc;
  if (!getJson(String(JOLPICA) + "/current/last/results.json", doc, 20000))
    return false;
  JsonObject r = doc["MRData"]["RaceTable"]["Races"][0];
  if (r.isNull()) return false;

  LastRace lr;
  lr.valid = true;
  strncpy(lr.name, r["raceName"] | "", sizeof(lr.name) - 1);
  JsonArray results = r["Results"];
  for (int i = 0; i < 5 && i < (int)results.size(); i++) {
    JsonObject res = results[i];
    lr.top[i].pos = atoi(res["position"] | "0");
    strncpy(lr.top[i].code, res["Driver"]["code"] | "---", 3);
    lr.top[i].code[3] = 0;
    strncpy(lr.top[i].team, res["Constructor"]["name"] | "",
            sizeof(lr.top[i].team) - 1);
  }
  lastRace = lr;
  Serial.printf("[api] last %s P1 %s\n", lr.name, lr.top[0].code);
  return true;
}

void loadDemo() {
  live = LiveState();
  live.haveSession = true;
  live.live = true;
  live.sessionKey = 11412;
  live.circuitKey = 14;
  strcpy(live.circuit, "Interlagos");
  strcpy(live.sessionName, "Race");
  live.totalLaps = 71;
  live.lap = 32;
  live.havePositions = true;
  live.haveWeather = true;
  live.air = 26.4;
  live.track = 41.2;
  live.humidity = 62;
  live.rain = false;
  live.flag = FLAG_YELLOW;
  strcpy(live.rcMsg, "YELLOW IN TRACK SECTOR 1");
  strcpy(live.radioCode, "RUS");
  live.radioAtMs = millis();

  live.lapBest = 78.442f;
  live.newsSeq = 1;
  LapNews& n = live.news[1 % NEWS_SLOTS];
  n.seq = 1;
  n.kind = NEWS_FASTEST;
  strcpy(n.code, "NOR");
  strcpy(n.time, "1:18.442");

  const char* codes[10] = {"VER", "NOR", "RUS", "ANT", "LEC", "HAM", "PIA", "SAI", "ALO", "GAS"};
  const char* teams[10] = {"Red Bull Racing", "McLaren", "Mercedes", "Mercedes",
                           "Ferrari", "Ferrari", "McLaren", "Williams",
                           "Aston Martin", "Alpine"};
  const uint16_t cols[10] = {rgb(54, 113, 198), rgb(255, 135, 0), rgb(0, 215, 182),
                             rgb(0, 215, 182), rgb(225, 6, 0), rgb(225, 6, 0),
                             rgb(255, 135, 0), rgb(0, 130, 250), rgb(0, 111, 98),
                             rgb(255, 117, 200)};
  const char* comps[10] = {"M", "M", "H", "S", "H", "M", "H", "S", "M", "H"};
  const char* gaps[10] = {"LEADER", "+2.431", "+6.204", "+7.881", "+14.532",
                          "+15.104", "+21.457", "+28.902", "+34.118", "+41.660"};
  const char* lasts[10] = {"VERSTAPPEN", "NORRIS", "RUSSELL", "ANTONELLI",
                           "LECLERC", "HAMILTON", "PIASTRI", "SAINZ",
                           "ALONSO", "GASLY"};
  uint8_t nums[10] = {1, 4, 63, 12, 16, 44, 81, 55, 14, 10};
  live.driverCount = 10;
  for (int i = 0; i < 10; i++) {
    DriverInfo& d = live.drivers[i];
    d = DriverInfo();
    d.num = nums[i];
    strncpy(d.code, codes[i], 3);
    strncpy(d.last, lasts[i], sizeof(d.last) - 1);
    strncpy(d.gap, gaps[i], sizeof(d.gap) - 1);
    d.gapLeader = i == 0;
    strncpy(d.team, teams[i], sizeof(d.team) - 1);
    d.colour = cols[i];
    d.target = strstr(d.team, "Mercedes") != nullptr;
    d.compound = comps[i][0];
    d.lapStart = i % 3 == 0 ? 28 : 18;
    live.pos[i] = i + 1;
  }

  nextRace = NextRace();
  nextRace.valid = true;
  nextRace.round = 21;
  strcpy(nextRace.name, "Las Vegas Grand Prix");
  strcpy(nextRace.locality, "Las Vegas");
  nextRace.start = time(nullptr) + 14 * 86400 + 12 * 3600 + 34 * 60;

  lastRace = LastRace();
  lastRace.valid = true;
  strcpy(lastRace.name, "Mexico City Grand Prix");
  const char* lc[5] = {"VER", "HAM", "ANT", "LEC", "NOR"};
  const char* lt[5] = {"Red Bull Racing", "Ferrari", "Mercedes", "Ferrari", "McLaren"};
  for (int i = 0; i < 5; i++) {
    lastRace.top[i].pos = i + 1;
    strncpy(lastRace.top[i].code, lc[i], 3);
    strncpy(lastRace.top[i].team, lt[i], sizeof(lastRace.top[i].team) - 1);
  }
}

}  // namespace f1
