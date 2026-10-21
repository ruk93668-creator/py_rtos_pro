#ifndef TEST_PY32_HAL_H
#define TEST_PY32_HAL_H
#include <stdint.h>
typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay);
#endif
