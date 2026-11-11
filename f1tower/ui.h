#pragma once
#include <Arduino.h>

namespace ui {
void init();
void backlight(uint8_t level);
void setDemo(bool on);
void splash(const char* title, const char* sub);
void error(const char* line1, const char* line2);
void liveFull();
void liveUpdate();
void idleShow(bool countdown);
void idleTick();
}
