#include "service_main.h"
#include "service_enmu.h"
#include "service_mpu_6050.h"
#include "service_display.h"
#include "key.h"

void ServiceMain_Init(void)
{
    KEY_Init();
    InputService_Init();
    DisplayService_Init();
    SensorService_Init();
    MotionService_Init();
}

void ServiceMain_Process(void)
{
    MotionService_Process();
    InputService_Process();
}
