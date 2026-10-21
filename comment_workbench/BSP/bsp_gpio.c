#include "bsp_gpio.h"

//×¢²á±í
static const BspGpioConfig_t gpiosconfig[gpio_num] = {
    [gpioa] = {
			.port = GPIOA,
      .pin = GPIO_PIN_3,
    },
    [gpios] = {
			.port = GPIOA,
      .pin = GPIO_PIN_4,
    },
		[gpiob] = {
			.port = GPIOA,
      .pin = GPIO_PIN_5,
    },
		[gpio_ble_sleep] =
		{
    .port = GPIOB,
    .pin  = GPIO_PIN_0
		},
};


HAL_StatusTypeDef BSP_GPIO_INIT(void){
	
  __HAL_RCC_GPIOA_CLK_ENABLE();     	
	/*****************************gpioa**********************************/ 
  GPIO_InitTypeDef  GPIOA_InitStruct = {0};
	
  GPIOA_InitStruct.Pin = gpiosconfig[gpioa].pin;
  GPIOA_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;            
  GPIOA_InitStruct.Pull = GPIO_PULLUP;                    /* Enable pull-up */
  GPIOA_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;          /* GPIO speed */
  /* GPIO initialization */
  HAL_GPIO_Init(gpiosconfig[gpioa].port, &GPIOA_InitStruct); 	
	
	/***************************gpios******************************************************************
	GPIO_MODE_IT_FALLING
	GPIO_PULLUP
	*/ 
  GPIO_InitTypeDef  GPIOS_InitStruct = {0};

  GPIOS_InitStruct.Pin = gpiosconfig[gpios].pin;
  GPIOS_InitStruct.Mode = GPIO_MODE_INPUT;            
  GPIOS_InitStruct.Pull = GPIO_PULLUP;                    /* Enable pull-up */
  GPIOS_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;          /* GPIO speed */
  /* GPIO initialization */
  HAL_GPIO_Init(gpiosconfig[gpios].port, &GPIOS_InitStruct); 
	
	/*****************************gpios**********************************/ 
  GPIO_InitTypeDef  GPIOB_InitStruct = {0};

  GPIOB_InitStruct.Pin = gpiosconfig[gpiob].pin;
  GPIOB_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;            
  GPIOB_InitStruct.Pull = GPIO_PULLUP;                    /* Enable pull-up */
  GPIOB_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;          /* GPIO speed */
  /* GPIO initialization */
  HAL_GPIO_Init(gpiosconfig[gpiob].port, &GPIOB_InitStruct); 	
	
	/*****************************gpio_ble_sleep**********************************/ 	
	__HAL_RCC_GPIOB_CLK_ENABLE();
	GPIO_InitTypeDef  GPIO_ble_InitStruct = {0};
	GPIO_ble_InitStruct.Pin   = GPIO_PIN_0;
	GPIO_ble_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_ble_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_ble_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

	HAL_GPIO_Init(GPIOB, &GPIO_ble_InitStruct);
	
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
	/**************** NVIC ****************/

	/* PA3 ¡ú EXTI3 ¡ú EXTI2_3_IRQn */
	HAL_NVIC_SetPriority(EXTI2_3_IRQn, 2, 0);
	HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);

	/* PA4¡¢PA5 ¡ú EXTI4/5 ¡ú ¹²ÓÃ EXTI4_15_IRQn */
	HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2, 0);
	HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
	
	return HAL_OK;
}

bool BSP_GPIO_READ(GpioId_t gpio){
	return HAL_GPIO_ReadPin(gpiosconfig[gpio].port, gpiosconfig[gpio].pin);
}

void BSP_GPIO_WRITE(GpioId_t gpio, bool state){
	HAL_GPIO_WritePin(gpiosconfig[gpio].port, gpiosconfig[gpio].pin, (GPIO_PinState)state);
}
