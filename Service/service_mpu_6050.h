#ifndef SERVICE_MPU_6050_H
#define SERVICE_MPU_6050_H

#include <stdint.h>

typedef struct {
    int16_t accel_x, accel_y, accel_z;
    int16_t gyro_x, gyro_y, gyro_z;
    uint32_t timestamp_ms;
    uint8_t valid;
} SensorData_t;

typedef enum {
    MOTION_NONE = 0, MOTION_UP, MOTION_DOWN, MOTION_LEFT,
    MOTION_RIGHT, MOTION_JUMP, MOTION_CROUCH, MOTION_SHAKE
} MotionCode_t;

#define MOTION_STATE_UP       0x01U
#define MOTION_STATE_DOWN     0x02U
#define MOTION_STATE_LEFT     0x04U
#define MOTION_STATE_RIGHT    0x08U

/* Init before scheduler; only Sensor task calls Process/MPU getters. */
void SensorService_Init(void);
void SensorService_Process(void);
uint8_t SensorService_Get(SensorData_t *data);
/* Motion processing and event consumption belong to the Service task. */
void MotionService_Init(void);
void MotionService_Process(void);
uint8_t MotionService_ConsumeEvent(MotionCode_t *event);
uint8_t MotionService_GetState(void);

#endif
