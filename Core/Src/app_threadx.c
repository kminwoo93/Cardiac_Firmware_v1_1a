/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
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
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"
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
TX_THREAD tx_app_thread;
TX_SEMAPHORE tx_app_semaphore;
/* USER CODE BEGIN PV */
extern volatile uint32_t drdy_irq_count;
volatile uint32_t ecg_thread_wakeup_count = 0;
volatile uint32_t semaphore_put_success_count = 0;
volatile uint32_t semaphore_put_error_count = 0;
volatile UINT semaphore_last_status = TX_SUCCESS;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN App_ThreadX_MEM_POOL */

  /* USER CODE END App_ThreadX_MEM_POOL */
  CHAR *pointer;

  /* Allocate the stack for ecg_acquisition_thread  */
  if (tx_byte_allocate(byte_pool, (VOID**) &pointer,
                       TX_APP_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }
  /* Create ecg_acquisition_thread.  */
  if (tx_thread_create(&tx_app_thread, "ecg_acquisition_thread", ecg_acquisition_thread_entry, 0, pointer,
                       TX_APP_STACK_SIZE, TX_APP_THREAD_PRIO, TX_APP_THREAD_PREEMPTION_THRESHOLD,
                       TX_APP_THREAD_TIME_SLICE, TX_APP_THREAD_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  /* Create ecg_drdy_semaphore.  */
  if (tx_semaphore_create(&tx_app_semaphore, "ecg_drdy_semaphore", 0) != TX_SUCCESS)
  {
    return TX_SEMAPHORE_ERROR;
  }

  /* USER CODE BEGIN App_ThreadX_Init */
  /* USER CODE END App_ThreadX_Init */

  return ret;
}
/**
  * @brief  Function implementing the ecg_acquisition_thread_entry thread.
  * @param  thread_input: Hardcoded to 0.
  * @retval None
  */
void ecg_acquisition_thread_entry(ULONG thread_input)
{
  /* USER CODE BEGIN ecg_acquisition_thread_entry */
	drdy_irq_count = 0;
	ecg_thread_wakeup_count = 0;
	semaphore_put_success_count = 0;
	semaphore_put_error_count = 0;
	/* ThreadX 실행 전에 쌓인 DRDY pending flag 제거 */
	  __HAL_GPIO_EXTI_CLEAR_IT(DRDY_Pin);
	  NVIC_ClearPendingIRQ(EXTI0_IRQn);

	  /* 이제 EXTI0 interrupt 시작 */
	  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

	  while (1)
	  {
		    if (tx_semaphore_get(&tx_app_semaphore,
		                         TX_WAIT_FOREVER) == TX_SUCCESS)
		    {
		        ecg_thread_wakeup_count++;
		    }
	  }

  /* USER CODE END ecg_acquisition_thread_entry */
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == DRDY_Pin)
	  {
	    UINT status;

	    status = tx_semaphore_put(&tx_app_semaphore);
	    semaphore_last_status = status;

	    if (status == TX_SUCCESS)
	    {
	      semaphore_put_success_count++;
	    }
	    else
	    {
	      semaphore_put_error_count++;
	    }
	  }
}
/* USER CODE END 1 */
