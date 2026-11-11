#pragma once
#include <Arduino.h>

// Reader for arrays of flat JSON objects (OpenF1 style). Works on a body that
// is already in RAM so parsing never touches the TLS socket.
class FlatSink {
 public:
  virtual ~FlatSink() {}
  virtual void startObject() {}
  virtual void field(const char* key, const char* val) {}
  virtual void endObject() {}
};

namespace jsonstream {

struct Cursor {
  const uint8_t* p;
  const uint8_t* end;
};

static int rd(Cursor& c) { return c.p < c.end ? *c.p++ : -1; }

static bool skipWs(Cursor& c, int* out) {
  while (true) {
    int ch = rd(c);
    if (ch < 0) return false;
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
    *out = ch;
    return true;
  }
}

static bool readStr(Cursor& c, char* buf, size_t n) {
  size_t i = 0;
  while (true) {
    int ch = rd(c);
    if (ch < 0) return false;
    if (ch == '"') {
      buf[i] = 0;
      return true;
    }
    if (ch == '\\') {
      int e = rd(c);
      if (e < 0) return false;
      char out = (char)e;
      if (e == 'u') {
        for (int k = 0; k < 4; k++) rd(c);
        out = '?';
      } else if (e == 'n' || e == 't' || e == 'r') {
        out = ' ';
      }
      if (i + 1 < n) buf[i++] = out;
      continue;
    }
    if (i + 1 < n) buf[i++] = (char)ch;
  }
}

// Reads a bare token; returns the delimiter that ended it, or -1 at end of body.
static int readToken(Cursor& c, int first, char* buf, size_t n) {
  size_t i = 0;
  int ch = first;
  while (ch >= 0) {
    if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\n' ||
        ch == '\r') {
      buf[i < n ? i : n - 1] = 0;
      return ch;
    }
    if (i + 1 < n) buf[i++] = (char)ch;
    ch = rd(c);
  }
  buf[i < n ? i : n - 1] = 0;
  return -1;
}

// Skips a nested object/array value (e.g. segments_sector_1). Fields holding
// containers are dropped rather than parsed.
static void skipContainer(Cursor& c, int first) {
  int depth = 0;
  bool inStr = false, esc = false;
  int ch = first;
  while (ch >= 0) {
    if (inStr) {
      if (esc) esc = false;
      else if (ch == '\\') esc = true;
      else if (ch == '"') inStr = false;
    } else if (ch == '"') {
      inStr = true;
    } else if (ch == '[' || ch == '{') {
      depth++;
    } else if ((ch == ']' || ch == '}') && --depth == 0) {
      return;
    }
    ch = rd(c);
  }
}

// Reads "[{...},{...}]" calling sink per object. True when the array closes.
static bool readArray(const uint8_t* data, size_t len, FlatSink& sink) {
  if (!data || len == 0) return true;  // empty result (OpenF1 404 -> no rows)
  Cursor c{data, data + len};
  int ch;
  if (!skipWs(c, &ch) || ch != '[') return false;

  while (true) {
    if (!skipWs(c, &ch)) return false;
    if (ch == ']') return true;
    if (ch == ',') continue;
    if (ch != '{') return false;

    sink.startObject();
    while (true) {
      if (!skipWs(c, &ch)) return false;
      if (ch == '}') break;
      if (ch == ',') continue;
      if (ch != '"') return false;

      char key[24];
      if (!readStr(c, key, sizeof(key))) return false;
      if (!skipWs(c, &ch) || ch != ':') return false;
      if (!skipWs(c, &ch)) return false;

      if (ch == '[' || ch == '{') {
        skipContainer(c, ch);
        continue;
      }

      char val[176];
      if (ch == '"') {
        if (!readStr(c, val, sizeof(val))) return false;
        sink.field(key, val);
        continue;
      }
      int delim = readToken(c, ch, val, sizeof(val));
      if (delim < 0) return false;
      sink.field(key, val);
      if (delim == '}') {
        sink.endObject();
        goto nextObject;
      }
      if (delim == ']') {
        sink.endObject();
        return true;
      }
    }
    sink.endObject();
  nextObject:;
  }
}

}  // namespace jsonstream
