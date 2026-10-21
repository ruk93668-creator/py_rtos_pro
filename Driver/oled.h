#ifndef _OLED_H
#define _OLED_H
#include <stdint.h>
#include <stdbool.h>

#include "py32f0xx_hal.h"


void OLED_Init(void);

void OLED_Clear(void);
void OLED_DrawPixel(int16_t x, int16_t y, uint8_t color);
void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void OLED_DrawRect(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color);
void OLED_FillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color);
void OLED_DrawBitmap(int16_t x, int16_t y, uint8_t width, uint8_t height,
                     const uint8_t *bitmap, uint8_t color);
void OLED_DrawChar(int16_t x, int16_t y, char character, uint8_t color);
void OLED_DrawString(int16_t x, int16_t y, const char *text, uint8_t color);

void OLED_Refresh(void);
#endif
