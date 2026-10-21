#include "game_util.h"
#include "service_display.h"

static uint16_t s_random = 0x4D3AU;

uint8_t GameUtil_WasPressed(InputCode_t code)
{
    return InputService_ConsumePress(code);
}

uint16_t GameUtil_Random(uint16_t limit)
{
    if (limit == 0U)
    {
        return 0U;
    }
    s_random = (uint16_t)((s_random << 1) ^ ((s_random & 0x8000U) != 0U ? 0x1021U : 0U));
    return (uint16_t)(s_random % limit);
}

uint16_t GameUtil_ClampDt(uint32_t dt_ms)
{
    if (dt_ms > 50U)
    {
        return 50U;
    }
    return (uint16_t)dt_ms;
}

void GameUtil_DrawSprite(int16_t x, int16_t y, const uint16_t *rows,
                         uint8_t width, uint8_t height)
{
    uint8_t row, col;

    if (rows == (const uint16_t *)0 || width == 0U || width > 16U ||
        height == 0U || height > 16U)
    {
        return;
    }
    for (row = 0U; row < height; ++row)
    {
        for (col = 0U; col < width; ++col)
        {
            if ((rows[row] & (uint16_t)(1U << (width - 1U - col))) != 0U)
            {
                DisplayService_Pixel((int16_t)(x + col), (int16_t)(y + row));
            }
        }
    }
}
