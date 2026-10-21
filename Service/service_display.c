#include "service_display.h"
#include "service_config.h"
#include "oled.h"

static uint32_t s_presented_at;
static uint8_t s_have_frame, s_drawing;

void DisplayService_Init(void)
{
    OLED_Init();
    s_presented_at = 0U;
    s_have_frame = s_drawing = 0U;
}

uint8_t DisplayService_BeginFrame(void)
{
    if (s_drawing || (s_have_frame &&
        (uint32_t)(HAL_GetTick() - s_presented_at) < SERVICE_DISPLAY_PERIOD_MS))
        return 0U;
    s_drawing = 1U;
    OLED_Clear();
    return 1U;
}

void DisplayService_Present(void)
{
    if (!s_drawing) return;
    s_presented_at = HAL_GetTick();
    OLED_Refresh();
    s_drawing = 0U;
    s_have_frame = 1U;
}

void DisplayService_Pixel(int16_t x, int16_t y) { OLED_DrawPixel(x, y, 1U); }
void DisplayService_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{ OLED_DrawLine(x0, y0, x1, y1, 1U); }
void DisplayService_Rect(int16_t x, int16_t y, int16_t width, int16_t height)
{ OLED_DrawRect(x, y, width, height, 1U); }
void DisplayService_FillRect(int16_t x, int16_t y, int16_t width, int16_t height)
{ OLED_FillRect(x, y, width, height, 1U); }
void DisplayService_Text(int16_t x, int16_t y, const char *text)
{ OLED_DrawString(x, y, text, 1U); }
void DisplayService_Number(int16_t x, int16_t y, uint16_t value, uint8_t digits)
{
    char text[6];
    uint8_t index;
    if (digits > 5U) digits = 5U;
    text[digits] = '\0';
    for (index = digits; index > 0U; --index) {
        text[index - 1U] = (char)('0' + value % 10U);
        value = (uint16_t)(value / 10U);
    }
    DisplayService_Text(x, y, text);
}
