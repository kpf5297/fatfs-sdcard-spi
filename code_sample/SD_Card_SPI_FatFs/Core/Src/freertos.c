/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "sd_functions.h"
#include "sd_benchmark.h"
#include "spi.h"
#include "gpio.h"
#include "usart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId ledBlinkTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartLEDBlinkTask(void const * argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  /* Run-time stack overflow checking is performed if
  configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
  called if a stack overflow is detected. */
  printf("\r\n[CRITICAL] Stack overflow in task: %s\r\n", pcTaskName);
  while(1);
}

void vApplicationMallocFailedHook(void)
{
  /* vApplicationMallocFailedHook() will only be called if
  configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
  function that will get called if a call to pvPortMalloc() fails.
  pvPortMalloc() is called internally by the kernel whenever a task, queue,
  timer or semaphore is created. It is also called by various parts of the
  demo application. If heap_1.c, heap_2.c or heap_4.c is being used, then the
  size of the heap available to pvPortMalloc() is defined by
  configTOTAL_HEAP_SIZE in FreeRTOSConfig.h, and the xPortGetFreeHeapSize()
  API function can be used to query the size of free heap space that remains
  (although it does not provide information on fragmentation). */
  printf("\r\n[CRITICAL] Malloc failed!\r\n");
  while(1);
}
/* USER CODE END 4 */
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* LED blink task - low priority, shows system is alive */
  osThreadDef(ledBlinkTask, StartLEDBlinkTask, osPriorityLow, 0, 256);
  ledBlinkTaskHandle = osThreadCreate(osThread(ledBlinkTask), NULL);
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  printf("\r\n");
  printf("[MAIN] Main task started\r\n");
  printf("[MAIN] Waiting 2 seconds before SD initialization...\r\n");
  osDelay(2000);
  
  printf("[MAIN] Initializing SD card system...\r\n");
    /* Enable DMA only if SPI DMA is configured and buffers are aligned. */
    if (sd_system_init(&hspi3, SPI1_CS_GPIO_Port, SPI1_CS_Pin, false) != 0) {
    printf("[ERROR] SD system initialization failed!\r\n");
    printf("[ERROR] Check: SPI3 connections, CS pin, power\r\n");
    while(1) {
      osDelay(1000);
    }
  }
  printf("[OK] SD system initialized\r\n\r\n");

  printf("[MAIN] Mounting SD card...\r\n");
  if (sd_mount() == FR_OK) {
    printf("[OK] SD card mounted successfully\r\n\r\n");
    printf("[MAIN] Testing file write...\r\n");
    if (sd_write_file("test.txt", "Hello from STM32F446!\r\n") == FR_OK) {
      printf("[OK] File written\r\n");
    } else {
      printf("[ERROR] File write failed\r\n");
    }
    
    printf("[MAIN] Demo complete. LED blink shows system alive.\r\n");
    printf("[MAIN] Main task complete, sleeping...\r\n\r\n");
    sd_unmount();
  } else {
    printf("[ERROR] Failed to mount SD card\r\n");
  }

  /* Infinite loop */
  for(;;)
  {
    osDelay(5000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  LED Blink Task - shows system is alive
  * @param  argument: Not used
  * @retval None
  */
void StartLEDBlinkTask(void const * argument)
{
  /* USER CODE BEGIN StartLEDBlinkTask */
  printf("[LED] Blink task started - LED on PA5 (LD2)\r\n");
  
  /* Infinite loop - blink LED every 1 second */
  for(;;)
  {
    /* LED on */
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    osDelay(500);
    
    /* LED off */
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    osDelay(500);
  }
  /* USER CODE END StartLEDBlinkTask */
}

/* USER CODE END Application */
