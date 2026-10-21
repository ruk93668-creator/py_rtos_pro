#ifndef SERVICE_DISPLAY_H
#define SERVICE_DISPLAY_H
#include <stdint.h>

/* One owner: the App task. Refresh uses the existing synchronous OLED API. */
void DisplayService_Init(void);
uint8_t DisplayService_BeginFrame(void);
void DisplayService_Present(void);
void DisplayService_Pixel(int16_t x, int16_t y);
void DisplayService_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void DisplayService_Rect(int16_t x, int16_t y, int16_t width, int16_t height);
void DisplayService_FillRect(int16_t x, int16_t y, int16_t width, int16_t height);
void DisplayService_Text(int16_t x, int16_t y, const char *text);
void DisplayService_Number(int16_t x, int16_t y, uint16_t value, uint8_t digits);
#endif
