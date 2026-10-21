#ifndef __OTA_UART_H
#define __OTA_UART_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    OTA_UART_TYPEC = 0,
    OTA_UART_BLE
} OtaUartPort_t;

bool OtaUart_Init(void);
void OtaUart_Process(void);
OtaUartPort_t OtaUart_GetPort(void);
bool OtaUart_SetPort(OtaUartPort_t port);
bool OtaUart_SendAck(uint16_t seq);
bool OtaUart_SendNack(uint16_t seq);
bool OtaUart_SendInfo(uint16_t seq);
#endif
