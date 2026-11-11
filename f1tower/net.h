#pragma once
#include <Arduino.h>

namespace net {

// Whole response body held in PSRAM; OpenF1 sends up to ~70 KB per endpoint.
struct Body {
  uint8_t* data = nullptr;
  size_t len = 0;
  size_t cap = 0;

  Body() = default;
  Body(const Body&) = delete;
  Body& operator=(const Body&) = delete;
  ~Body() { release(); }
  void release() {
    if (data) free(data);
    data = nullptr;
    len = cap = 0;
  }
};

bool connect(uint32_t timeoutMs = 25000);
bool online();
bool timeReady();
// GET the url and keep the whole body (dechunked) in RAM. 200 on success.
int getBody(const String& url, Body& out, uint32_t timeoutMs = 15000);
}
