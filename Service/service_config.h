#ifndef SERVICE_CONFIG_H
#define SERVICE_CONFIG_H

/* Service policy only. Hardware setup remains in the existing BSP/Driver. */
#define SERVICE_SENSOR_PERIOD_MS       20U /* MPU_INIT configures 50 Hz. */
#define SERVICE_SENSOR_STALE_MS        100U
#define SERVICE_SENSOR_RETRY_MS        1000U
#define SERVICE_DISPLAY_PERIOD_MS      40U
#define SERVICE_BUTTON_DEBOUNCE_MS     20U
#define SERVICE_BUTTON_BACK_MS         600U
#define SERVICE_ROTARY_HOLD_MS         150U
#define SERVICE_MENU_DAMPING_MS        180U
#define SERVICE_MENU_START_GUARD_MS    500U

/* Current MPU driver: +/-2 g and +/-2000 degrees/s. */
#define SERVICE_TILT_ENTER_RAW         7000
#define SERVICE_TILT_LEAVE_RAW         4500
#define SERVICE_TILT_STABLE_MS         30U
#define SERVICE_JUMP_GYRO_RAW          1750
#define SERVICE_SHAKE_GYRO_RAW         3000
#define SERVICE_GYRO_REARM_RAW         750
#define SERVICE_JUMP_COOLDOWN_MS       350U
#define SERVICE_SHAKE_COOLDOWN_MS      1000U
/* No mounting reversal is specified by this project; keep device axes. */
#define SERVICE_MOTION_INVERT_X        0
#define SERVICE_MOTION_INVERT_Y        0

#endif
