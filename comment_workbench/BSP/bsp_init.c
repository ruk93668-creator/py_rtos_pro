#include "bsp_gpio.h"
#include "bsp_iic.h"
#include "bsp_init.h"
#include "bsp_usart.h"

/*-------------------------------------------------------
 * 返回值:
 * 0 成功
 * 1 失败
 *------------------------------------------------------*/
uint8_t bsp_init(void)
{
	 if ( BSP_GPIO_INIT() != HAL_OK)
		 return 1;	
	 if ( BSP_I2C1_INIT() != HAL_OK)
		 return 1;
	 if ( BSP_I2C2_INIT() != HAL_OK)
		 return 1;
	 if (BSP_UART0_INIT() != HAL_OK)
        return 1;
	 if (BSP_UART1_INIT() != HAL_OK)
        return 1;
	 return 0;
}
