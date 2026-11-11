#pragma once
#include <TFT_eSPI.h>

// Draws the team lockup with primitives (no embedded artwork): three-pointed
// star, four speed slashes, AMG, PETRONAS, FORMULA 1 TEAM. Proportions are
// taken from the official mark (337 x 370 reference), scaled to `width`.
// Returns the drawn height in pixels.
int drawTeamLogo(TFT_eSPI& tft, int x, int top, int width, uint16_t bg);
