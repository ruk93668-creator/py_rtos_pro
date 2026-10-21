#ifndef GAME_UTIL_H
#define GAME_UTIL_H

#include <stdint.h>
#include "service_enmu.h"

uint8_t GameUtil_WasPressed(InputCode_t code);
uint16_t GameUtil_Random(uint16_t limit);
uint16_t GameUtil_ClampDt(uint32_t dt_ms);
/* Draw a small 1bpp sprite. The leftmost pixel is the most significant bit. */
void GameUtil_DrawSprite(int16_t x, int16_t y, const uint16_t *rows,
                         uint8_t width, uint8_t height);

#endif
