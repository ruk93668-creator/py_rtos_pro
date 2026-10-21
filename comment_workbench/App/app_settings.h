#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdint.h>

/* The numeric values are the application-level flag exposed to the rest of
   the firmware.  Type-C remains the default used by the current OTA driver. */
typedef enum
{
    APP_UART_TYPEC = 0U,
    APP_UART_BLE   = 1U
} AppUartPort_t;

void AppSettings_Init(void);
void AppSettings_Process(void);
void AppSettings_Render(void);

void AppSettings_SetUart(AppUartPort_t port);
AppUartPort_t AppSettings_GetUart(void);
uint8_t AppSettings_GetUartFlag(void);

/* Reads the version recorded for the currently running slot. */
uint8_t AppSettings_GetCurrentVersion(uint32_t *version);

/* Returns non-zero when the Settings page was closed with A or Back. */
uint8_t AppSettings_IsDone(void);
void AppSettings_ClearDone(void);

#endif
