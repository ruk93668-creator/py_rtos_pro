#include "bsp_usart.h"
#include <stddef.h>
#include <string.h>

#if (BSP_UART_RX_BUFFER_SIZE < 2U) || (BSP_UART_RX_BUFFER_SIZE > 65535U)
#error Invalid BSP_UART_RX_BUFFER_SIZE
#endif
#if (BSP_UART_TX_MAX_SIZE < 1U) || (BSP_UART_TX_MAX_SIZE > 6553U)
#error Invalid BSP_UART_TX_MAX_SIZE
#endif

/* Preserve the board wiring. RX and TX need not use the same peripheral. */
static const BspUartConfig_t gpiosconfig[uart_num] = {
    [uart_ble] = {
        .port = GPIOA,
        .pin_rx = GPIO_PIN_1,
        .pin_tx = GPIO_PIN_2,
    },
    [uart_typec] = {
        .port = GPIOA,
        .pin_rx = GPIO_PIN_10,
        .pin_tx = GPIO_PIN_11,
    },
};

typedef struct {
    uint8_t data[BSP_UART_RX_BUFFER_SIZE];
    uint8_t irq_byte;
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflows;
    volatile uint32_t errors;
    volatile bool initialized;
    volatile HAL_StatusTypeDef tx_status;
} BspUartState_t;

static BspUartState_t s_state[uart_num];
static UART_HandleTypeDef s_ble_rx;
static UART_HandleTypeDef s_ble_tx;
static UART_HandleTypeDef s_typec_rx;
static TIM_HandleTypeDef s_typec_timer;
static uint8_t s_ble_tx_data[BSP_UART_TX_MAX_SIZE];
/* 10 bits per byte, plus three idle bits to drain the CCR preload pipeline. */
static uint16_t s_typec_bits[BSP_UART_TX_MAX_SIZE * 10U + 3U];
static uint16_t s_typec_bit_ticks;
static const uint16_t * volatile s_typec_next_bit;
static const uint16_t *s_typec_bit_end;

static bool BSP_UART_ValidId(UartId_t id)
{
    return (uint32_t)id < (uint32_t)uart_num;
}

static void BSP_UART_ConfigPin(UartId_t id, uint16_t pin, uint32_t af)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = af;
    HAL_GPIO_Init(gpiosconfig[id].port, &gpio);
}

static HAL_StatusTypeDef BSP_UART_ConfigHandle(UART_HandleTypeDef *handle,
                                               USART_TypeDef *instance,
                                               uint32_t baud, uint32_t mode)
{
    handle->Instance = instance;
    handle->Init.BaudRate = baud;
    handle->Init.WordLength = UART_WORDLENGTH_8B;
    handle->Init.StopBits = UART_STOPBITS_1;
    handle->Init.Parity = UART_PARITY_NONE;
    handle->Init.Mode = mode;
    handle->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    handle->Init.OverSampling = UART_OVERSAMPLING_16;
    handle->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    return HAL_UART_Init(handle);
}

static void BSP_UART_EnableIRQ(IRQn_Type irq)
{
    HAL_NVIC_SetPriority(irq, BSP_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_ClearPendingIRQ(irq);
    HAL_NVIC_EnableIRQ(irq);
}

static void BSP_UART_TypecTxStop(HAL_StatusTypeDef status)
{
    TIM1->DIER = 0U;
    CLEAR_BIT(TIM1->CR1, TIM_CR1_CEN);
    /* Latch idle high before making the buffer available to the next send. */
    TIM1->CCR4 = s_typec_bit_ticks;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CNT = 0U;
    TIM1->SR = 0U;
    HAL_NVIC_ClearPendingIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    s_state[uart_typec].tx_status = status;
}

static HAL_StatusTypeDef BSP_UART_TypecTxInit(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef oc = {0};
    RCC_ClkInitTypeDef clock_config = {0};
    uint32_t latency;
    uint32_t timer_clock;
    uint32_t ticks;

    HAL_RCC_GetClockConfig(&clock_config, &latency);
    timer_clock = HAL_RCC_GetPCLK1Freq();
    if (clock_config.APB1CLKDivider != RCC_HCLK_DIV1) {
        timer_clock *= 2U;
    }
    if (BSP_UART_TYPEC_BAUDRATE == 0U) {
        return HAL_ERROR;
    }
    ticks = (timer_clock + BSP_UART_TYPEC_BAUDRATE / 2U) /
            BSP_UART_TYPEC_BAUDRATE;
    if ((ticks < 2U) || (ticks > 65535U)) {
        return HAL_ERROR;
    }
    s_typec_bit_ticks = (uint16_t)ticks;

    /* Hold idle high until the timer output has been fully configured. */
    HAL_GPIO_WritePin(gpiosconfig[uart_typec].port,
                      gpiosconfig[uart_typec].pin_tx, GPIO_PIN_SET);
    gpio.Pin = gpiosconfig[uart_typec].pin_tx;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(gpiosconfig[uart_typec].port, &gpio);

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM1_FORCE_RESET();
    __HAL_RCC_TIM1_RELEASE_RESET();

    s_typec_timer.Instance = TIM1;
    s_typec_timer.Init.Prescaler = 0U;
    s_typec_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_typec_timer.Init.Period = ticks - 1U;
    s_typec_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_typec_timer.Init.RepetitionCounter = 0U;
    s_typec_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&s_typec_timer) != HAL_OK) {
        return HAL_ERROR;
    }
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = ticks;             /* CCR > ARR gives a constant logic one. */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_SET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&s_typec_timer, &oc, TIM_CHANNEL_4) != HAL_OK) {
        return HAL_ERROR;
    }

    /* Only counter overflow requests an IRQ; software UG just loads registers.
     * Hardware latches CCR4 at each bit boundary. The update ISR supplies the
     * next bit's preload, without a HAL timer callback or a DMA channel.
     */
    TIM1->DIER = 0U;
    SET_BIT(TIM1->CR1, TIM_CR1_URS);
    SET_BIT(TIM1->CCMR2, TIM_CCMR2_OC4PE);
    TIM1->CCR4 = ticks;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;
    SET_BIT(TIM1->CCER, TIM_CCER_CC4E);
    SET_BIT(TIM1->BDTR, TIM_BDTR_MOE | TIM_BDTR_OSSI);
    BSP_UART_ConfigPin(uart_typec, gpiosconfig[uart_typec].pin_tx, GPIO_AF2_TIM1);
    return HAL_OK;
}

void BSP_UART_FlushRx(UartId_t id)
{
    uint32_t saved_mask;

    if (!BSP_UART_ValidId(id))
    {
        return;
    }

    saved_mask = __get_PRIMASK();

    __disable_irq();

    /*
     * tail追上head，
     * 表示当前所有已接收数据全部丢弃。
     */
    s_state[id].tail =
        s_state[id].head;

    __set_PRIMASK(saved_mask);
}

HAL_StatusTypeDef BSP_UART0_INIT(void)
{
    if (s_state[uart_ble].initialized) {
        return HAL_OK;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART4_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    HAL_NVIC_DisableIRQ(USART3_4_IRQn);
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    BSP_UART_ConfigPin(uart_ble, gpiosconfig[uart_ble].pin_rx, GPIO_AF4_USART4);
    BSP_UART_ConfigPin(uart_ble, gpiosconfig[uart_ble].pin_tx, GPIO_AF1_USART2);
    if (BSP_UART_ConfigHandle(&s_ble_rx, USART4, BSP_UART_BLE_BAUDRATE,
                             UART_MODE_RX) != HAL_OK ||
        BSP_UART_ConfigHandle(&s_ble_tx, USART2, BSP_UART_BLE_BAUDRATE,
                             UART_MODE_TX) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_UART_Receive_IT(&s_ble_rx, &s_state[uart_ble].irq_byte, 1U) != HAL_OK) {
        return HAL_ERROR;
    }
    s_state[uart_ble].tx_status = HAL_OK;
    s_state[uart_ble].initialized = true;
    BSP_UART_EnableIRQ(USART3_4_IRQn);
    BSP_UART_EnableIRQ(USART2_IRQn);
    return HAL_OK;
}

HAL_StatusTypeDef BSP_UART1_INIT(void)
{
    if (s_state[uart_typec].initialized) {
        return HAL_OK;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    BSP_UART_ConfigPin(uart_typec, gpiosconfig[uart_typec].pin_rx, GPIO_AF1_USART1);
    if (BSP_UART_ConfigHandle(&s_typec_rx, USART1, BSP_UART_TYPEC_BAUDRATE,
                             UART_MODE_RX) != HAL_OK ||
        BSP_UART_TypecTxInit() != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_UART_Receive_IT(&s_typec_rx, &s_state[uart_typec].irq_byte, 1U) != HAL_OK) {
        return HAL_ERROR;
    }
    s_state[uart_typec].tx_status = HAL_OK;
    s_state[uart_typec].initialized = true;
    BSP_UART_EnableIRQ(USART1_IRQn);
    /* Bit timing must preempt the UART RX, EXTI and HAL tick handlers. */
    HAL_NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn,
                         BSP_UART_TYPEC_TX_IRQ_PRIORITY, 0U);
    HAL_NVIC_ClearPendingIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    HAL_NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    return HAL_OK;
}

bool BSP_UART_ReadByte(UartId_t id, uint8_t *byte)
{
    BspUartState_t *state;
    uint32_t saved_mask;
    uint16_t next;
    if (!BSP_UART_ValidId(id) || byte == NULL || !s_state[id].initialized) {
        return false;
    }
    state = &s_state[id];
    saved_mask = __get_PRIMASK();
    __disable_irq();
    if (state->tail == state->head) {
        __set_PRIMASK(saved_mask);
        return false;
    }
    *byte = state->data[state->tail];
    next = (uint16_t)(state->tail + 1U);
    state->tail = (next == BSP_UART_RX_BUFFER_SIZE) ? 0U : next;
    __set_PRIMASK(saved_mask);
    return true;
}

uint32_t BSP_UART_GetRxOverflowCount(UartId_t id)
{
    return BSP_UART_ValidId(id) ? s_state[id].overflows : 0U;
}

uint32_t BSP_UART_GetRxErrorCount(UartId_t id)
{
    return BSP_UART_ValidId(id) ? s_state[id].errors : 0U;
}

HAL_StatusTypeDef BSP_UART_GetTxStatus(UartId_t id)
{
    if (!BSP_UART_ValidId(id) || !s_state[id].initialized) {
        return HAL_ERROR;
    }
    return s_state[id].tx_status;
}

HAL_StatusTypeDef BSP_UART_Transmit_IT(UartId_t id,
                                     const uint8_t *data, uint16_t size)
{
    HAL_StatusTypeDef status;
    uint32_t saved_mask;
    uint16_t count = 0U;
    uint16_t i;
    uint8_t bit;

    if (!BSP_UART_ValidId(id) || !s_state[id].initialized || data == NULL ||
        size == 0U || size > BSP_UART_TX_MAX_SIZE || __get_IPSR() != 0U) {
        return HAL_ERROR;
    }
    saved_mask = __get_PRIMASK();
    __disable_irq();
    if (s_state[id].tx_status == HAL_BUSY) {
        __set_PRIMASK(saved_mask);
        return HAL_BUSY;
    }
    s_state[id].tx_status = HAL_BUSY;
    __set_PRIMASK(saved_mask);

    if (id == uart_ble) {
        memcpy(s_ble_tx_data, data, size);
        status = HAL_UART_Transmit_IT(&s_ble_tx, s_ble_tx_data, size);
    } else {
        for (i = 0U; i < size; ++i) {
            s_typec_bits[count++] = 0U;  /* Start bit. */
            for (bit = 0U; bit < 8U; ++bit) {
                s_typec_bits[count++] = ((data[i] >> bit) & 1U) ?
                                        s_typec_bit_ticks : 0U;
            }
            s_typec_bits[count++] = s_typec_bit_ticks; /* Stop bit. */
        }
        s_typec_bits[count++] = s_typec_bit_ticks;
        s_typec_bits[count++] = s_typec_bit_ticks;
        s_typec_bits[count++] = s_typec_bit_ticks;

        TIM1->DIER = 0U;
        CLEAR_BIT(TIM1->CR1, TIM_CR1_CEN);
        /* One initial idle period lets hardware time the first falling edge. */
        TIM1->CCR4 = s_typec_bit_ticks;
        TIM1->EGR = TIM_EGR_UG;
        TIM1->CNT = 0U;
        TIM1->SR = 0U;
        HAL_NVIC_ClearPendingIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
        s_typec_next_bit = &s_typec_bits[1];
        s_typec_bit_end = &s_typec_bits[count];
        /* The first overflow outputs START; its ISR queues data bit zero. */
        TIM1->CCR4 = s_typec_bits[0];
        __DMB();
        TIM1->DIER = TIM_DIER_UIE;
        SET_BIT(TIM1->CR1, TIM_CR1_CEN);
        status = HAL_OK;
    }
    if (status != HAL_OK) {
        s_state[id].tx_status = HAL_ERROR;
    }
    return status;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    BspUartState_t *state;
    uint16_t next;
    if (handle == &s_ble_rx) {
        state = &s_state[uart_ble];
    } else if (handle == &s_typec_rx) {
        state = &s_state[uart_typec];
    } else {
        return;
    }
    if (handle->ErrorCode != HAL_UART_ERROR_NONE) {
        return; /* Discard bytes marked with framing/parity/noise errors. */
    }
    next = (uint16_t)(state->head + 1U);
    if (next == BSP_UART_RX_BUFFER_SIZE) {
        next = 0U;
    }
    if (next == state->tail) {
        ++state->overflows;
    } else {
        state->data[state->head] = state->irq_byte;
        __DMB();
        state->head = next;
    }
    /* Rearm after HAL_UART_IRQHandler returns, so an ORE error cannot
     * abort a new receive started inside this completion callback.
     */
}

static void BSP_UART_RxIRQ(UART_HandleTypeDef *handle, UartId_t id)
{
    const uint32_t error_mask = USART_SR_PE | USART_SR_FE |
                                USART_SR_NE | USART_SR_ORE;
    if ((handle->Instance->SR & error_mask) != 0U) {
        ++s_state[id].errors;
    }
    HAL_UART_IRQHandler(handle);
    if ((handle->Instance->SR & error_mask) != 0U) {
        __HAL_UART_CLEAR_OREFLAG(handle); /* SR/DR sequence clears RX errors. */
    }
    if (handle->RxState == HAL_UART_STATE_READY) {
        (void)HAL_UART_Receive_IT(handle, &s_state[id].irq_byte, 1U);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle == &s_ble_tx) {
        s_state[uart_ble].tx_status = HAL_OK;
    }
}

void USART1_IRQHandler(void)
{
    if (s_state[uart_typec].initialized) {
        BSP_UART_RxIRQ(&s_typec_rx, uart_typec);
    }
}

void USART2_IRQHandler(void)
{
    if (s_state[uart_ble].initialized) {
        HAL_UART_IRQHandler(&s_ble_tx);
    }
}

void USART3_4_IRQHandler(void)
{
    /* USART3 is unused; this BSP owns the USART4 part of the shared vector. */
    if (s_state[uart_ble].initialized) {
        BSP_UART_RxIRQ(&s_ble_rx, uart_ble);
    }
}

/* At 8 MHz/115200 baud there are only 69 core cycles per bit. Keep the
 * per-bit path optimized even when the application is built at -O0.
 * No HAL or RTOS calls belong in that path; the stop helper runs only once.
 */
#if defined(__CC_ARM)
#pragma push
#pragma O2
#endif
void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
    const uint16_t *next;

    if ((TIM1->SR & TIM_SR_UIF) == 0U) {
        return;
    }
    TIM1->SR = 0U;
    next = s_typec_next_bit;
    if (next != s_typec_bit_end) {
        TIM1->CCR4 = *next;
        s_typec_next_bit = next + 1;
        return;
    }
    /* All data and the last stop bit have elapsed; idle is already active. */
    BSP_UART_TypecTxStop(HAL_OK);
}
#if defined(__CC_ARM)
#pragma pop
#endif
