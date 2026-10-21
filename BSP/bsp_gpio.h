#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#include "py32f0xx_hal.h"

typedef enum{
    gpioa = 0,
    gpios,
		gpiob,
		gpio_ble_sleep,
	
    gpio_num,
		
}GpioId_t;

typedef struct
{
    GPIO_TypeDef  *port;
		uint16_t			pin;
} BspGpioConfig_t;

HAL_StatusTypeDef BSP_GPIO_INIT(void);
bool BSP_GPIO_READ(GpioId_t gpio);
void BSP_GPIO_WRITE(GpioId_t gpio, bool state);
#endif
