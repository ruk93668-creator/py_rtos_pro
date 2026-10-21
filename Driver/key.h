#ifndef KEY_H
#define KEY_H

#include <stdint.h>
#include <stdbool.h>

void KEY_Init(void);

/* 取走累计的旋转格数，读取后自动清零 */
int8_t KEY_TakeRotation(void);

/* 获取编码器按键状态：1 = 按下，0 = 松开 */
bool KEY_IsPressed(void);

#endif
