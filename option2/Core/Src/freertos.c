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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "latency.h"
#include <stdio.h>
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

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

#define LOAD_BASE 10000

void vLoadTask(void *pvParameters) {
    uint32_t load_iterations;
    volatile uint32_t dummy = 0;

    for (;;) {
        load_iterations = LOAD_BASE * current_load_factor;

        for (uint32_t i = 0; i < load_iterations; i++) {
            dummy = i * i;
        }

        osDelay(1);
    }
}

extern osSemaphoreId_t micSemHandle;

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC1) {
        uint32_t current_tick = xTaskGetTickCount();
        latency_record_interrupt(current_tick);
        osSemaphoreRelease(micSemHandle);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC1) {
        uint32_t current_tick = xTaskGetTickCount();
        latency_record_interrupt(current_tick);
        osSemaphoreRelease(micSemHandle);
    }
}

/* USER CODE END Application */

