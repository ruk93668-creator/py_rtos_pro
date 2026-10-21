#include <string.h>
#include "bsp_iic.h"
#include "oled.h"

static const uint8_t s_font_digits[10][5] =
{
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}
};

static const uint8_t s_font_upper[26][5] =
{
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
};

#define OLED_IIC_ID    		 iic_2
#define OLED_CTRL_CMD      0x00U
#define OLED_CTRL_DATA     0x40U
#define OLED_ADDR      		 0x3CU

#define OLED_WIDTH      128U
#define OLED_HEIGHT      32U
#define OLED_PAGES        4U

static uint8_t s_framebuffer[OLED_WIDTH * OLED_HEIGHT / 8U];
//写指令
static HAL_StatusTypeDef OLED_WriteCommands(uint8_t *data, uint8_t len)
{
    return BSP_IIC_WriteRegs(
        OLED_IIC_ID,
        OLED_ADDR,
        OLED_CTRL_CMD,
        data,
        len
    );
}
//写数据
static HAL_StatusTypeDef OLED_WriteData(uint8_t *data, uint8_t len)
{
    return BSP_IIC_WriteRegs(
        OLED_IIC_ID,
        OLED_ADDR,
        OLED_CTRL_DATA,
        data,
        len
    );
}

void OLED_Init(void)
{
    static uint8_t init_commands[] =
    {
        0xAE,             /* Display OFF */

        0xD5, 0x80,       /* Display Clock */

        0xA8, 0x1F,       /* Multiplex Ratio = 32 */

        0xD3, 0x00,       /* Display Offset = 0 */

        0x40,             /* Start Line = 0 */

        0x8D, 0x14,       /* Charge Pump ON */

        0x20, 0x02,       /* Page Addressing Mode */

        0xA0,             /* Segment Remap */
        0xC0,             /* COM Scan Direction */

        0xDA, 0x02,       /* COM Pins: 128x32 */

        0x81, 0x8F,       /* Contrast */

        0xD9, 0xF1,       /* Pre-charge */

        0xDB, 0x30,       /* VCOMH */

        0xA4,             /* Follow GDDRAM */

        0xA6,             /* Normal Display */

        0xAF              /* Display ON */
    };

    memset(s_framebuffer, 0, sizeof(s_framebuffer));

    OLED_WriteCommands(
        init_commands,
        (uint8_t)sizeof(init_commands)
    );

    OLED_Refresh();//oled clear
}
//一次性刷新屏幕
void OLED_Refresh(void)
{
    uint8_t page;
    uint8_t commands[3];

    for (page = 0U; page < OLED_PAGES; page++)
    {
        /*
         * 设置当前 Page
         * Column = 0
         */
        commands[0] = (uint8_t)(0xB0U + page);
        commands[1] = 0x00U;
        commands[2] = 0x10U;

        OLED_WriteCommands(
            commands,
            sizeof(commands)
        );

        /*
         * 一次发送整个 Page
         * 128 Byte
         */
        OLED_WriteData(
            &s_framebuffer[
                (uint16_t)page * OLED_WIDTH
            ],
            OLED_WIDTH
        );
    }
}

void OLED_Clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}
//打点
void OLED_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
    uint16_t index;
    uint8_t mask;

    if ((x < 0) || (x >= (int16_t)OLED_WIDTH) ||
        (y < 0) || (y >= (int16_t)OLED_HEIGHT))
    {
        return;
    }
    index = (uint16_t)(((uint16_t)(y >> 3) * OLED_WIDTH) + (uint16_t)x);
    mask = (uint8_t)(1U << (y & 7));
    if (color != 0U)
    {
        s_framebuffer[index] |= mask;
    }
    else
    {
        s_framebuffer[index] &= (uint8_t)~mask;
    }
}
//连线
void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t dx;
    int16_t sx;
    int16_t dy;
    int16_t sy;
    int16_t err;
    int16_t e2;

    dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    sx = (x0 < x1) ? 1 : -1;
    dy = (y0 < y1) ? (y0 - y1) : (y1 - y0);
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;
    for (;;)
    {
        OLED_DrawPixel(x0, y0, color);
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }
        e2 = (int16_t)(2 * err);
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}
//空心矩形
void OLED_DrawRect(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color)
{
    if ((width <= 0) || (height <= 0))
    {
        return;
    }
    OLED_DrawLine(x, y, (int16_t)(x + width - 1), y, color);
    OLED_DrawLine(x, (int16_t)(y + height - 1), (int16_t)(x + width - 1),
                  (int16_t)(y + height - 1), color);
    OLED_DrawLine(x, y, x, (int16_t)(y + height - 1), color);
    OLED_DrawLine((int16_t)(x + width - 1), y, (int16_t)(x + width - 1),
                  (int16_t)(y + height - 1), color);
}
//实心矩形
void OLED_FillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color)
{
    int16_t xx;
    int16_t yy;

    for (yy = y; yy < (int16_t)(y + height); yy++)
    {
        for (xx = x; xx < (int16_t)(x + width); xx++)
        {
            OLED_DrawPixel(xx, yy, color);
        }
    }
}
//
void OLED_DrawBitmap(int16_t x, int16_t y, uint8_t width, uint8_t height,
                     const uint8_t *bitmap, uint8_t color)
{
    uint8_t row_bytes;
    uint8_t xx;
    uint8_t yy;

    if (bitmap == (const uint8_t *)0)
    {
        return;
    }
    row_bytes = (uint8_t)((width + 7U) / 8U);
    for (yy = 0U; yy < height; yy++)
    {
        for (xx = 0U; xx < width; xx++)
        {
            if ((bitmap[(yy * row_bytes) + (xx / 8U)] & (uint8_t)(0x80U >> (xx & 7U))) != 0U)
            {
                OLED_DrawPixel((int16_t)(x + xx), (int16_t)(y + yy), color);
            }
        }
    }
}

void OLED_DrawChar(int16_t x, int16_t y, char character, uint8_t color)
{
    uint8_t glyph[5];
    uint8_t column;
    uint8_t row;
    uint8_t found;

    memset(glyph, 0, sizeof(glyph));
    found = 1U;
    if ((character >= 'a') && (character <= 'z'))
    {
        character = (char)(character - ('a' - 'A'));
    }
    if ((character >= '0') && (character <= '9'))
    {
        memcpy(glyph, s_font_digits[character - '0'], 5U);
    }
    else if ((character >= 'A') && (character <= 'Z'))
    {
        memcpy(glyph, s_font_upper[character - 'A'], 5U);
    }
    else if (character == ':')
    {
        glyph[1] = 0x36U;
        glyph[3] = 0x36U;
    }
    else if (character == '.')
    {
        glyph[2] = 0x60U;
    }
    else if (character == '-')
    {
        glyph[1] = 0x08U;
        glyph[2] = 0x08U;
        glyph[3] = 0x08U;
    }
    else if (character == '!')
    {
        glyph[2] = 0x5FU;
    }
    else if (character == '?')
    {
        glyph[0] = 0x02U;
        glyph[1] = 0x01U;
        glyph[2] = 0x51U;
        glyph[3] = 0x09U;
        glyph[4] = 0x06U;
    }
    else if (character == '>')
    {
        glyph[1] = 0x22U;
        glyph[2] = 0x14U;
        glyph[3] = 0x08U;
    }
    else if (character == '<')
    {
        glyph[1] = 0x08U;
        glyph[2] = 0x14U;
        glyph[3] = 0x22U;
    }
    else if (character == '=')
    {
        glyph[1] = 0x14U;
        glyph[2] = 0x14U;
        glyph[3] = 0x14U;
    }
    else if (character != ' ')
    {
        found = 0U;
    }
    if (found == 0U)
    {
        glyph[0] = 0x7FU;
        glyph[1] = 0x41U;
        glyph[2] = 0x41U;
        glyph[3] = 0x41U;
        glyph[4] = 0x7FU;
    }
    for (column = 0U; column < 5U; column++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
            {
                OLED_DrawPixel((int16_t)(x + column), (int16_t)(y + row), color);
            }
        }
    }
}

void OLED_DrawString(int16_t x, int16_t y, const char *text, uint8_t color)
{
    int16_t cursor_x;
    int16_t cursor_y;

    if (text == (const char *)0)
    {
        return;
    }
    cursor_x = x;
    cursor_y = y;
    while (*text != '\0')
    {
        if (*text == '\n')
        {
            cursor_x = x;
            cursor_y += 8;
        }
        else
        {
            OLED_DrawChar(cursor_x, cursor_y, *text, color);
            cursor_x += 6;
        }
        text++;
    }
}

