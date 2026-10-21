#include "ble.h"
#include "bsp_gpio.h"

void BLE_Init(void)
{
    BLE_Wakeup();
}

void BLE_Wakeup(void)
{
    BSP_GPIO_WRITE(gpio_ble_sleep, 0);
}

void BLE_Sleep(void)
{
    BSP_GPIO_WRITE(gpio_ble_sleep, 1);
}
