#ifndef TEST_BSP_IIC_H
#define TEST_BSP_IIC_H
#include "py32f0xx_hal.h"
typedef enum { iic_1 = 0, iic_2, iic_num } IICId_t;
HAL_StatusTypeDef BSP_IIC_WriteRegs(IICId_t id, uint16_t addr, uint8_t reg,
                                    uint8_t *data, uint8_t len);
#endif
