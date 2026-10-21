#ifndef BSP_IIC_H
#define BSP_IIC_H

#include <stdint.h>
#include <stdbool.h>

#include "py32f0xx_hal.h"
#include "py32f071_hal_i2c.h"


typedef enum
{
    iic_1 = 0,
    iic_2,
    iic_num
} IICId_t;


typedef struct
{
    I2C_HandleTypeDef *handle;
    GPIO_TypeDef *port;
    uint16_t pin_scl;
    uint16_t pin_sda;
} BspIICConfig_t;

/* BSP lifecycle. */
HAL_StatusTypeDef BSP_I2C1_INIT(void);
HAL_StatusTypeDef BSP_I2C2_INIT(void);
HAL_StatusTypeDef BSP_IIC_WriteReg(IICId_t id, uint16_t addr, uint8_t reg, uint8_t val);
HAL_StatusTypeDef BSP_IIC_ReadReg(IICId_t id, uint16_t addr, uint8_t reg, uint8_t *pval);
HAL_StatusTypeDef BSP_IIC_ReadRegs(IICId_t id, uint16_t addr, uint8_t reg, uint8_t *data, uint8_t len);
HAL_StatusTypeDef BSP_IIC_WriteRegs(IICId_t id, uint16_t addr, uint8_t reg, uint8_t *data, uint8_t len);
HAL_StatusTypeDef BSP_IIC_Read(IICId_t id, uint16_t addr, uint8_t *data, uint16_t len);
HAL_StatusTypeDef BSP_IIC_Write(IICId_t id, uint16_t addr, uint8_t *data, uint16_t len);
#endif /* BSP_IIC_H */
