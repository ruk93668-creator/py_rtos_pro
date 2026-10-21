#include "service_mpu_6050.h"
#include "service_config.h"
#include "mpu_6050.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static SensorData_t s_data;
static uint8_t s_sensor_ready;
static uint32_t s_next_sample, s_retry_at;

void SensorService_Init(void)
{
    /* Before scheduler: no RTOS critical section around HAL_Delay/I2C. */
    memset(&s_data, 0, sizeof(s_data));
    s_sensor_ready = (uint8_t)(MPU_INIT() == 0U);
    s_next_sample = HAL_GetTick();
    s_retry_at = s_next_sample + SERVICE_SENSOR_RETRY_MS;
}

static int16_t Filter(int16_t old_value, int16_t new_value)
{
    return (int16_t)((int32_t)old_value +
                    ((int32_t)new_value - old_value) / 4);
}

void SensorService_Process(void)
{
    uint32_t now = HAL_GetTick();
    short ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    SensorData_t next;

    if (!s_sensor_ready) {
        if ((int32_t)(now - s_retry_at) < 0) return;
        s_sensor_ready = (uint8_t)(MPU_INIT() == 0U);
        now = HAL_GetTick();
        s_retry_at = now + SERVICE_SENSOR_RETRY_MS;
        s_next_sample = now;
        if (!s_sensor_ready) return;
    }
    if ((int32_t)(now - s_next_sample) < 0) return;

    /* Existing Driver is the only route to the sensor. It requires the
       separately reviewed MPU_Read_Len fix before hardware integration. */
    if (MPU_Get_Accelerometer(&ax, &ay, &az) != 0U ||
        MPU_Get_Gyroscope(&gx, &gy, &gz) != 0U) {
        taskENTER_CRITICAL();
        s_data.valid = 0U;
        taskEXIT_CRITICAL();
        s_sensor_ready = 0U;
        s_retry_at = HAL_GetTick() + SERVICE_SENSOR_RETRY_MS;
        return;
    }
    now = HAL_GetTick();
    s_next_sample = now + SERVICE_SENSOR_PERIOD_MS;
    /* Sensor task is the only writer. Readers take the same short lock. */
    next.accel_x = ax; next.accel_y = ay; next.accel_z = az;
    next.gyro_x = gx; next.gyro_y = gy; next.gyro_z = gz;
    if (s_data.valid && (uint32_t)(now - s_data.timestamp_ms) <= SERVICE_SENSOR_STALE_MS) {
        next.accel_x = Filter(s_data.accel_x, ax);
        next.accel_y = Filter(s_data.accel_y, ay);
        next.accel_z = Filter(s_data.accel_z, az);
        /* Keep angular-rate spikes intact.  Jump/shake are edge events and
           filtering them can erase a one-sample gesture at the 50 Hz rate;
           acceleration remains filtered for stable tilt control. */
        next.gyro_x = gx;
        next.gyro_y = gy;
        next.gyro_z = gz;
    }
    next.timestamp_ms = now;
    next.valid = 1U;
    taskENTER_CRITICAL();
    s_data = next;
    taskEXIT_CRITICAL();
}

uint8_t SensorService_Get(SensorData_t *data)
{
    if (!data) return 0U;
    taskENTER_CRITICAL();
    *data = s_data;
    taskEXIT_CRITICAL();
    if (!data->valid || (uint32_t)(HAL_GetTick() - data->timestamp_ms) > SERVICE_SENSOR_STALE_MS) {
        data->valid = 0U;
        return 0U;
    }
    return 1U;
}

static uint8_t s_state, s_candidate, s_have_sample, s_calibrated;
static uint8_t s_jump_armed, s_shake_armed;
static int16_t s_base_x, s_base_y;
static uint32_t s_changed, s_timestamp, s_jump_at, s_shake_at;
static uint16_t s_events;

static int32_t Abs(int32_t value) { return value < 0 ? -value : value; }

static uint8_t Axis(int32_t value, uint8_t positive, uint8_t negative)
{
    if (value > SERVICE_TILT_ENTER_RAW) return positive;
    if (value < -SERVICE_TILT_ENTER_RAW) return negative;
    if (value > SERVICE_TILT_LEAVE_RAW) return s_state & positive;
    if (value < -SERVICE_TILT_LEAVE_RAW) return s_state & negative;
    return 0U;
}

void MotionService_Init(void)
{
    s_state = s_candidate = s_have_sample = s_calibrated = 0U;
    s_base_x = s_base_y = 0;
    s_jump_armed = s_shake_armed = 1U;
    s_changed = s_timestamp = 0U;
    s_jump_at = s_shake_at = HAL_GetTick();
    s_events = 0U;
}

void MotionService_Process(void)
{
    SensorData_t data;
    uint32_t now = HAL_GetTick();
    uint8_t candidate, entered;
    int32_t accel_x, accel_y;
    if (!SensorService_Get(&data)) {
        s_state = s_candidate = s_have_sample = s_calibrated = 0U;
        s_events = 0U;
        s_jump_armed = s_shake_armed = 1U;
        s_jump_at = s_shake_at = now;
        return;
    }
    if (s_have_sample && data.timestamp_ms == s_timestamp) return;
    s_have_sample = 1U;
    s_timestamp = data.timestamp_ms;
    if (!s_calibrated) {
        s_base_x = data.accel_x;
        s_base_y = data.accel_y;
        s_calibrated = 1U;
        s_changed = now;
        return;
    }
    accel_x = (int32_t)data.accel_x - s_base_x;
    accel_y = (int32_t)data.accel_y - s_base_y;
#if SERVICE_MOTION_INVERT_X
    accel_x = -accel_x;
#endif
#if SERVICE_MOTION_INVERT_Y
    accel_y = -accel_y;
#endif
    /* Keep axes independent; mounting adjustments live in Service policy. */
    candidate = Axis(accel_x, MOTION_STATE_RIGHT, MOTION_STATE_LEFT) |
                Axis(accel_y, MOTION_STATE_DOWN, MOTION_STATE_UP);
    if (candidate != s_candidate) { s_candidate = candidate; s_changed = now; }
    if ((uint32_t)(now - s_changed) >= SERVICE_TILT_STABLE_MS && s_state != candidate) {
        entered = candidate & (uint8_t)~s_state;
        s_state = candidate;
        if (entered & MOTION_STATE_UP) s_events |= 1U << MOTION_UP;
        if (entered & MOTION_STATE_DOWN) s_events |= (1U << MOTION_DOWN) | (1U << MOTION_CROUCH);
        if (entered & MOTION_STATE_LEFT) s_events |= 1U << MOTION_LEFT;
        if (entered & MOTION_STATE_RIGHT) s_events |= 1U << MOTION_RIGHT;
    }
    if (s_jump_armed && Abs(data.gyro_x) > SERVICE_JUMP_GYRO_RAW &&
        Abs(data.accel_z) < 12500 && (int32_t)(now - s_jump_at) >= 0) {
        s_events |= 1U << MOTION_JUMP;
        s_jump_armed = 0U;
        s_jump_at = now + SERVICE_JUMP_COOLDOWN_MS;
    } else if (Abs(data.gyro_x) < SERVICE_GYRO_REARM_RAW && Abs(data.accel_z) > 13500)
        s_jump_armed = 1U;
    if (s_shake_armed && Abs(data.gyro_z) > SERVICE_SHAKE_GYRO_RAW &&
        (int32_t)(now - s_shake_at) >= 0) {
        s_events |= 1U << MOTION_SHAKE;
        s_shake_armed = 0U;
        s_shake_at = now + SERVICE_SHAKE_COOLDOWN_MS;
    } else if (Abs(data.gyro_z) < SERVICE_GYRO_REARM_RAW) s_shake_armed = 1U;
}

uint8_t MotionService_ConsumeEvent(MotionCode_t *event)
{
    unsigned i;
    if (!event) return 0U;
    for (i = 1U; i <= MOTION_SHAKE; ++i) {
        if (s_events & (1U << i)) {
            s_events &= (uint16_t)~(1U << i);
            *event = (MotionCode_t)i;
            return 1U;
        }
    }
    return 0U;
}

uint8_t MotionService_GetState(void) { return s_state; }
