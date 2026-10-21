#include "bsp_iic.h"
#include <stddef.h>

static I2C_HandleTypeDef s_hi2c1;
static I2C_HandleTypeDef s_hi2c2;

#define BSP_I2C_TIMEOUT  50u     /* 单次收发超时(ms) */
//注册要使用的iIC
static const BspIICConfig_t s_iic_config[iic_num] =
{
    [iic_1] =
    {
        .handle  = &s_hi2c1,
        .port    = GPIOB,
        .pin_scl = GPIO_PIN_6,
        .pin_sda = GPIO_PIN_7,
    },
    [iic_2] =
    {
        .handle  = &s_hi2c2,
        .port    = GPIOB,
        .pin_scl = GPIO_PIN_8,
        .pin_sda = GPIO_PIN_9,
    },
};

HAL_StatusTypeDef BSP_I2C1_INIT(void)
{
	IICId_t id = iic_1;//选择配置
	GPIO_InitTypeDef gpio = {0};
	
  __HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_I2C1_CLK_ENABLE();

	gpio.Pin = s_iic_config[id].pin_scl |
						 s_iic_config[id].pin_sda;
	gpio.Mode = GPIO_MODE_AF_OD;
	gpio.Pull = GPIO_PULLUP;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	gpio.Alternate = GPIO_AF1_I2C1;
	HAL_GPIO_Init(s_iic_config[id].port, &gpio);
	
	s_iic_config[id].handle->Instance 						 = I2C1;
	s_iic_config[id].handle->Init.ClockSpeed 		 = 400000U;
	s_iic_config[id].handle->Init.DutyCycle       = I2C_DUTYCYCLE_2;
	s_iic_config[id].handle->Init.OwnAddress1     = 0U;
	s_iic_config[id].handle->Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
	s_iic_config[id].handle->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	s_iic_config[id].handle->Init.OwnAddress2     = 0U;
	s_iic_config[id].handle->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	s_iic_config[id].handle->Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
	
	return HAL_I2C_Init(s_iic_config[id].handle);
}

HAL_StatusTypeDef BSP_I2C2_INIT(void)
{
	IICId_t id = iic_2;//选择配置
	GPIO_InitTypeDef gpio = {0};
	
  __HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_I2C2_CLK_ENABLE();

	gpio.Pin = s_iic_config[id].pin_scl |
						 s_iic_config[id].pin_sda;
	gpio.Mode = GPIO_MODE_AF_OD;
	gpio.Pull = GPIO_PULLUP;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	gpio.Alternate = GPIO_AF13_I2C2;
	HAL_GPIO_Init(s_iic_config[id].port, &gpio);
	
	s_iic_config[id].handle->Instance 						 = I2C2;
	s_iic_config[id].handle->Init.ClockSpeed 		 = 400000U;
	s_iic_config[id].handle->Init.DutyCycle       = I2C_DUTYCYCLE_2;
	s_iic_config[id].handle->Init.OwnAddress1     = 0U;
	s_iic_config[id].handle->Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
	s_iic_config[id].handle->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	s_iic_config[id].handle->Init.OwnAddress2     = 0U;
	s_iic_config[id].handle->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	s_iic_config[id].handle->Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
	
	return HAL_I2C_Init(s_iic_config[id].handle);
}
/***********************************
bsp包装
**/
HAL_StatusTypeDef BSP_IIC_WriteReg(IICId_t id, uint16_t addr, uint8_t reg, uint8_t val)
{
	return HAL_I2C_Mem_Write(s_iic_config[id].handle, addr << 1U, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1u,
                              BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef BSP_IIC_ReadReg(IICId_t id, uint16_t addr, uint8_t reg, uint8_t *pval)
{
	return HAL_I2C_Mem_Read(s_iic_config[id].handle, addr << 1U, reg,
                              I2C_MEMADD_SIZE_8BIT, pval, 1u,
                              BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef BSP_IIC_ReadRegs(IICId_t id, uint16_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    return HAL_I2C_Mem_Read(s_iic_config[id].handle, addr << 1U, reg,
                            I2C_MEMADD_SIZE_8BIT, data, len,
                            BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef BSP_IIC_WriteRegs(IICId_t id, uint16_t addr, uint8_t reg, uint8_t *data, uint8_t len)
{
	return HAL_I2C_Mem_Write(s_iic_config[id].handle, addr << 1U, reg,
                              I2C_MEMADD_SIZE_8BIT, data, len,
                              BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef BSP_IIC_Read(IICId_t id, uint16_t addr, uint8_t *data, uint16_t len)
{
	return HAL_I2C_Master_Receive(s_iic_config[id].handle,
																	addr << 1U, data, len,
																	BSP_I2C_TIMEOUT);
}

HAL_StatusTypeDef BSP_IIC_Write(IICId_t id, uint16_t addr, uint8_t *data, uint16_t len)
{
	return HAL_I2C_Master_Transmit(s_iic_config[id].handle,
																	addr << 1U, data, len,
																	BSP_I2C_TIMEOUT);
}

