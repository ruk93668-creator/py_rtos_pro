#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdint.h>
#include <stdbool.h>
#include "py32f0xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Both links use 8 data bits, no parity, 1 stop bit, no flow control. */
#define BSP_UART_BLE_BAUDRATE       38400U
#define BSP_UART_TYPEC_BAUDRATE     38400U
#define BSP_UART_IRQ_PRIORITY      2U
#define BSP_UART_TYPEC_TX_IRQ_PRIORITY 0U
#define BSP_UART_RX_BUFFER_SIZE    269u
#define BSP_UART_TX_MAX_SIZE       60U

typedef enum {
    uart_ble = 0,
    uart_typec,
    uart_num
} UartId_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin_rx;
    uint16_t pin_tx;
} BspUartConfig_t;

/*
 * Call after HAL_Init/SystemClock_Config, before starting application tasks.
 * Repeated successful initialization is a no-op.
 *
 * BLE:   PA1 = USART4_RX (AF4), PA2 = USART2_TX (AF1).
 * Type-C: PA10 = USART1_RX (AF1), PA11 = TIM1_CH4 (AF2).
 * PA11 has no UART TX function: TIM1 update interrupts generate 8N1 TX.
 * This BSP owns USART1/2/4 and TIM1. Do not reuse them.
 */
HAL_StatusTypeDef BSP_UART0_INIT(void);
HAL_StatusTypeDef BSP_UART1_INIT(void);

/* Non-blocking receive. One task should consume each port's RX buffer.
 * Usable capacity is BSP_UART_RX_BUFFER_SIZE - 1 bytes.
 */
bool BSP_UART_ReadByte(UartId_t id, uint8_t *byte);
uint32_t BSP_UART_GetRxOverflowCount(UartId_t id);
uint32_t BSP_UART_GetRxErrorCount(UartId_t id);

/* Task context only. Copies 1..BSP_UART_TX_MAX_SIZE bytes before returning.
 * HAL_OK means accepted; HAL_BUSY means an earlier send is still active.
 * Check GetTxStatus for completion (HAL_OK), pending (HAL_BUSY), or error.
 * Type-C TX uses TIM1 update interrupts; BLE TX uses USART2 interrupts.
 * Type-C needs one ISR per bit while sending (about 8.68 us at 115200).
 * Long global interrupt masking, including RTOS critical sections, can
 * stretch bits. The timer ISR does not call HAL timer or RTOS APIs.
 */
HAL_StatusTypeDef BSP_UART_Transmit_IT(UartId_t id,
                                     const uint8_t *data, uint16_t size);
HAL_StatusTypeDef BSP_UART_GetTxStatus(UartId_t id);

void BSP_UART_FlushRx(UartId_t id);

#ifdef __cplusplus
}
#endif
#endif
