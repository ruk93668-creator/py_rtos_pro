#ifndef DRIVER_MAIN_H
#define DRIVER_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Call after BSP_IIC_Init().
 * This layer contains only device drivers and the scheduling glue between
 * device state machines and the lower BSP I2C message queue.
 */
void Driver_MainInit(void);
void Driver_MainProcess(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_MAIN_H */
