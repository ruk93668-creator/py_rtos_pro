#include "driver_main.h"
#include "bsp_iic.h"
#include "mpu_6050.h"
#include "oled.h"

void Driver_MainInit(void)
{
    MPU6050_Init();
    OLED_Init();
}

void Driver_MainProcess(void)
{
    /* Finish/start queued bus transactions first. */
    BSP_IIC_Process();

    /* Advance device-level non-blocking state machines. */
    MPU6050_Process();
    OLED_Process();

    /* Start a message that a driver may have queued in this iteration. */
    BSP_IIC_Process();
}
