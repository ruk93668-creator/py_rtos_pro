/**
  ******************************************************************************
  * @file    main.c
  * @author  MCU Application Team
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_init.h"
#include "service_main.h"
#include "service_mpu_6050.h"
#include "app_main.h"


#include "boot_metadata.h"
#include "ota_protocol.h"
#include "ota_uart.h"
#include "bsp_usart.h"
/* Private define ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private user code ---------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
static void APP_SystemClockConfig(void);
static void Task1(void *pvParamters);
static void Task2(void *pvParamters);
static void Task3(void *pvParamters);
static void OtaUartTask(void *argument);

/**
  * @brief  Main program.
  * @retval int
  */
int main(void)
{
  /* Reset of all peripherals, Initializes the Systick */
  HAL_Init();

  /* System clock configuration */
  APP_SystemClockConfig();

  if (bsp_init() != 0U)
  {
    APP_ErrorHandler();
  }
  ServiceMain_Init();
  AppMain_Init();
	
	if (!OtaUart_Init())
	{
		APP_ErrorHandler();
	}
  /* Initialize UART */
//  BSP_USART_Config();

  /* Create the tasks that are created using the original xTaskCreate() API function. */
	if ((xTaskCreate(Task1, "Task1", 256, NULL, 1, NULL) != pdPASS) ||
			(xTaskCreate(Task2, "Task2", 192, NULL, 2, NULL) != pdPASS) ||
			(xTaskCreate(Task3, "Task3", 192, NULL, 3, NULL) != pdPASS) ||
			(xTaskCreate(OtaUartTask, "OtaUart", 256, NULL, 3, NULL) != pdPASS))
  {
    APP_ErrorHandler();
  }
	
  /* Start the scheduler. */
  vTaskStartScheduler();
  APP_ErrorHandler();
	

  return true;
}

/**
  * @brief  Output log,express task1 is running.
  * @param  *pvParamters: The parameters passed to the task function when the task is created
  * @retval None
  */
static void Task1(void *pvParamters)
{
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t start_tick = xTaskGetTickCount();

    uint32_t last_process_ms = HAL_GetTick();

    bool boot_confirmed = false;

    (void)pvParamters;

    while (1)
    {
        uint32_t now_ms = HAL_GetTick();

        AppMain_Process(
            now_ms - last_process_ms);

        last_process_ms = now_ms;


        /*
         * APP真正运行3秒以后再确认当前Slot
         */
        if ((!boot_confirmed) &&
            ((xTaskGetTickCount() - start_tick) >=
             pdMS_TO_TICKS(3000U)))
        {
            if (App_ConfirmCurrentSlot())
            {
                boot_confirmed = true;
            }
        }


        if ((TickType_t)(
                xTaskGetTickCount() - last_wake) >=
            pdMS_TO_TICKS(10U))
        {
            last_wake =
                xTaskGetTickCount();
        }

        vTaskDelayUntil(
            &last_wake,
            pdMS_TO_TICKS(10U));
    }
}

/**
  * @brief  Output log,express Task2 is running.
  * @param  *pvParamters: The parameters passed to the task function when the task is created
  * @retval None
  */
static void Task2(void *pvParamters)
{
  TickType_t last_wake = xTaskGetTickCount();
  (void)pvParamters;

  while (1)
  {
    ServiceMain_Process();
    if ((TickType_t)(xTaskGetTickCount() - last_wake) >= pdMS_TO_TICKS(5U))
    {
      last_wake = xTaskGetTickCount();
    }
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5U));
  }
}

/**
  * @brief  Output log,express Task3 is running.
  * @param  *pvParamters: The parameters passed to the task function when the task is created
  * @retval None
  */
static void Task3(void *pvParamters)
{
  TickType_t last_wake = xTaskGetTickCount();
  (void)pvParamters;

  while (1)
  {
    SensorService_Process();
    if ((TickType_t)(xTaskGetTickCount() - last_wake) >= pdMS_TO_TICKS(20U))
    {
      last_wake = xTaskGetTickCount();
    }
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20U));
  }
}
static void OtaUartTask(void *argument)
{
    (void)argument;

    while (1)
    {
        OtaUart_Process();

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
/**
  * @brief  System clock configuration function
  * @param  None
  * @retval None
  */
static void APP_SystemClockConfig(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* Oscillator configuration */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE; /* Select oscillator HSE, HSI, LSI, LSE */
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                          /* Enable HSI */
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                          /* HSI 1 frequency division */
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_24MHz; /* HSI 24MHz feeds PLL x3 for SYSCLK 72MHz. */
  RCC_OscInitStruct.HSEState = RCC_HSE_OFF;                         /* Close HSE */
  /*RCC_OscInitStruct.HSEFreq = RCC_HSE_16_32MHz;*/
  RCC_OscInitStruct.LSIState = RCC_LSI_OFF;                         /* Close LSI */
  RCC_OscInitStruct.LSEState = RCC_LSE_OFF;                         /* Close LSE */
  /*RCC_OscInitStruct.LSEDriver = RCC_LSEDRIVE_MEDIUM;*/
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;                      /* Enable PLL */
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL3;
  /* Configure oscillator */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  /* Clock source configuration */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1; /* Choose to configure clock HCLK, SYSCLK, PCLK1 */
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; /* Select PLL 72MHz as the system clock. */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;     /* AHB clock 1 division */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;      /* APB clock 1 division */
  /* Configure clock source */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    APP_ErrorHandler();
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void APP_ErrorHandler(void)
{
  /* Infinite loop */
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file��pointer to the source file name
  * @param  line��assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     for example: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* Infinite loop */
  while (1)
  {
  }
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
