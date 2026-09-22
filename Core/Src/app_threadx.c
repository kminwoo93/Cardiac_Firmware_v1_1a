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
#include "ads1292r.h"
#include "icm20948.h"
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
TX_QUEUE ecg_sample_queue;
TX_QUEUE scg_sample_queue;

static ULONG ecg_queue_storage[
    ECG_QUEUE_CAPACITY * ECG_QUEUE_MESSAGE_SIZE
];
static ULONG scg_queue_storage[
    SCG_QUEUE_CAPACITY * SCG_QUEUE_MESSAGE_SIZE
];

extern volatile uint32_t drdy_irq_count;
volatile uint32_t ecg_thread_wakeup_count = 0;
volatile uint32_t semaphore_put_success_count = 0;
volatile uint32_t semaphore_put_error_count = 0;
volatile UINT semaphore_last_status = TX_SUCCESS;
volatile uint8_t ecg_raw[9] = {0};
volatile int32_t ecg_ch1_raw = 0;
volatile int32_t ecg_ch2_raw = 0;

volatile uint32_t ecg_valid_frame_count = 0;
volatile uint32_t ecg_invalid_frame_count = 0;

volatile uint32_t ecg_queue_send_count = 0;
volatile uint32_t ecg_queue_drop_count = 0;
volatile UINT ecg_queue_last_status = TX_SUCCESS;

volatile uint32_t scg_queue_send_count = 0U;
volatile uint32_t scg_queue_drop_count = 0U;
volatile UINT scg_queue_last_status = TX_SUCCESS;

/*
 * SCG acquisition thread control block.
 */
TX_THREAD scg_acquisition_thread;

/*
 * Counting semaphore used to transfer ICM Data Ready events
 * from the ISR to the SCG acquisition thread.
 */
TX_SEMAPHORE scg_drdy_semaphore;

/*
 * This becomes 1 only after scg_drdy_semaphore has been created.
 */
volatile uint8_t scg_sync_ready = 0U;

/*
 * Defined in stm32u5xx_it.c.
 */
extern volatile uint32_t icm_drdy_irq_count;

/*
 * Diagnostic counters.
 */
volatile uint32_t scg_thread_wakeup_count = 0U;

volatile uint32_t scg_semaphore_put_success_count = 0U;
volatile uint32_t scg_semaphore_put_error_count = 0U;

volatile uint32_t scg_spi_success_count = 0U;
volatile uint32_t scg_spi_error_count = 0U;

/*
 * Most recent return values.
 */
volatile UINT scg_semaphore_last_status = TX_SUCCESS;

volatile HAL_StatusTypeDef scg_spi_last_status = HAL_OK;

/*
 * Most recently acquired raw accelerometer values.
 */
volatile int16_t scg_accel_x_raw = 0;
volatile int16_t scg_accel_y_raw = 0;
volatile int16_t scg_accel_z_raw = 0;

volatile uint32_t scg_last_sample_tick = 0U;
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
  	  /*
       * Create ECG sample queue.
       *
       * Message size is expressed in ULONG units:
       * 4 ULONG = 16 bytes = sizeof(ECG_Sample).
       */
  if (sizeof(ECG_Sample) !=
      (ECG_QUEUE_MESSAGE_SIZE * sizeof(ULONG)))
  {
      return TX_SIZE_ERROR;
  }

  if (tx_queue_create(&ecg_sample_queue,
                      "ecg_sample_queue",
                      ECG_QUEUE_MESSAGE_SIZE,
                      ecg_queue_storage,
                      sizeof(ecg_queue_storage)) != TX_SUCCESS)
  {
      return TX_QUEUE_ERROR;
  }
  /*
   * Allocate SCG thread stack.
   */
  CHAR *scg_stack_pointer;

  if (tx_byte_allocate(
          byte_pool,
          (VOID **)&scg_stack_pointer,
          SCG_THREAD_STACK_SIZE,
          TX_NO_WAIT) != TX_SUCCESS)
  {
      return TX_POOL_ERROR;
  }

  /*
   * Create a counting semaphore with zero initial tokens.
   * The SCG thread must wait for an actual Data Ready interrupt.
   */
  if (tx_semaphore_create(
          &scg_drdy_semaphore,
          "scg_drdy_semaphore",
          0U) != TX_SUCCESS)
  {
      return TX_SEMAPHORE_ERROR;
  }

  /*
   * Create the SCG acquisition thread.
   */
  if (tx_thread_create(
          &scg_acquisition_thread,
          "scg_acquisition_thread",
          scg_acquisition_thread_entry,
          0U,
          scg_stack_pointer,
          SCG_THREAD_STACK_SIZE,
          SCG_THREAD_PRIORITY,
          SCG_THREAD_PRIORITY,
          TX_NO_TIME_SLICE,
          TX_AUTO_START) != TX_SUCCESS)
  {
      return TX_THREAD_ERROR;
  }

  /*
   * Prevent ISR access while synchronization is being prepared.
   */
  scg_sync_ready = 0U;

  /*
   * Remove interrupts that occurred before the semaphore was ready.
   */
  __HAL_GPIO_EXTI_CLEAR_IT(ICM_INT_Pin);
  NVIC_ClearPendingIRQ(ICM_INT_EXTI_IRQn);

  /*
   * Start all diagnostic counters from the same point.
   */
  icm_drdy_irq_count = 0U;
  scg_thread_wakeup_count = 0U;
  scg_semaphore_put_success_count = 0U;
  scg_semaphore_put_error_count = 0U;
  scg_spi_success_count = 0U;
  scg_spi_error_count = 0U;

  /*
   * The ISR may now safely put semaphore tokens.
   */
  scg_sync_ready = 1U;

  /*
   * Confirm that SCG_Sample has the exact size expected
   * by the ThreadX queue.
   */
  if (sizeof(SCG_Sample) !=
      (SCG_QUEUE_MESSAGE_SIZE * sizeof(ULONG)))
  {
      return TX_SIZE_ERROR;
  }

  /*
   * Create the SCG sample queue.
   */
  if (tx_queue_create(
          &scg_sample_queue,
          "scg_sample_queue",
          SCG_QUEUE_MESSAGE_SIZE,
          scg_queue_storage,
          sizeof(scg_queue_storage)) != TX_SUCCESS)
  {
      return TX_QUEUE_ERROR;
  }

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
	ECG_Sample sample;

	ADS1292R_CH2FilterState ch2_filter;
	ADS1292R_CH2FilterOutput ch2_output;

	uint32_t sample_counter = 0;
	TX_PARAMETER_NOT_USED(thread_input);

	uint8_t ecg_raw[9];


	drdy_irq_count = 0;
	ecg_thread_wakeup_count = 0;
	semaphore_put_success_count = 0;
	semaphore_put_error_count = 0;

	ecg_valid_frame_count = 0;
	ecg_invalid_frame_count = 0;
	ecg_queue_send_count = 0;
	ecg_queue_drop_count = 0;
	ecg_queue_last_status = TX_SUCCESS;
	/*Filter initialization*/
	ADS1292R_CH2FilterInit(&ch2_filter);
	sample_counter = 0;
	/* ThreadX 실행 전에 쌓인 DRDY pending flag 제거 */
	  __HAL_GPIO_EXTI_CLEAR_IT(DRDY_Pin);
	  NVIC_ClearPendingIRQ(EXTI0_IRQn);




	  while (1)
	  {
		    if (tx_semaphore_get(&tx_app_semaphore,
		                         TX_WAIT_FOREVER) == TX_SUCCESS)
		    {

		    	ecg_thread_wakeup_count++;

		    	    ADS1292R_ReadData((uint8_t *)ecg_raw);

		    	    /*
		    	     * Valid ADS1292R status starts with 1100.
		    	     */
		    	    if ((ecg_raw[0] & 0xF0U) == 0xC0U)
		    	    {
		    	        ecg_valid_frame_count++;
		    	        sample_counter++;

		    	        /*
		    	         * Convert the two 24-bit ADC channels.
		    	         */
		    	        ecg_ch1_raw = ADS1292R_Convert24Bit(
		    	            ecg_raw[3],
		    	            ecg_raw[4],
		    	            ecg_raw[5]);

		    	        ecg_ch2_raw = ADS1292R_Convert24Bit(
		    	            ecg_raw[6],
		    	            ecg_raw[7],
		    	            ecg_raw[8]);

		    	        /*
		    	         * Process every valid CH2 sample.
		    	         * Filter coefficients are designed for fs = 500 Hz.
		    	         */
		    	        (void)ADS1292R_ProcessCH2Sample(
		    	            &ch2_filter,
		    	            ecg_ch2_raw,
		    	            &ch2_output);

		    	        /*
		    	         * Fill one queue message.
		    	         */
		    	        sample.sample_counter = sample_counter;
                        sample.timestamp_ms = HAL_GetTick();

		    	        sample.ch1_raw = ecg_ch1_raw;
		    	        sample.ch2_raw = ecg_ch2_raw;

		    	        sample.ch2_bandpass =
		    	            (int32_t)ch2_output.bandpass;

		    	        sample.ch2_notch =
		    	            (int32_t)ch2_output.notch;

		    	        sample.ch2_bandpass_notch =
		    	            (int32_t)ch2_output.bandpass_notch;

		    	        sample.ch2_all_filter =
		    	            (int32_t)ch2_output.all_filter;

		    	        /*
		    	         * Do not block ECG acquisition when queue is full.
		    	         */
		    	        ecg_queue_last_status =
		    	            tx_queue_send(
		    	                &ecg_sample_queue,
		    	                &sample,
		    	                TX_NO_WAIT);



		    	        if (ecg_queue_last_status == TX_SUCCESS)
		    	        {
		    	            ecg_queue_send_count++;
		    	        }
		    	        else
		    	        {
		    	            ecg_queue_drop_count++;
		    	        }
		    	    }
		    	    else
		    	    {
		    	        ecg_invalid_frame_count++;
		    	    }
		    }
	  }

  /* USER CODE END ecg_acquisition_thread_entry */
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */

void scg_acquisition_thread_entry(ULONG thread_input)
{
	ICM20948_AccelRaw accel_sample;
	    SCG_Sample sample;

	    uint32_t sample_counter = 0U;
	    TX_PARAMETER_NOT_USED(thread_input);

	    scg_thread_wakeup_count = 0U;
	    scg_spi_success_count = 0U;
	    scg_spi_error_count = 0U;

	    scg_queue_send_count = 0U;
	    scg_queue_drop_count = 0U;
	    scg_queue_last_status = TX_SUCCESS;

	    while (1)
	    {
	        if (tx_semaphore_get(
	                &scg_drdy_semaphore,
	                TX_WAIT_FOREVER) == TX_SUCCESS)
	        {
	            scg_thread_wakeup_count++;

	            /*
	             * Read exactly one XYZ sample after one
	             * Data Ready event.
	             */
	            scg_spi_last_status =
	                ICM20948_ReadAccelRaw(&accel_sample);

	            if (scg_spi_last_status == HAL_OK)
	            {
	                scg_spi_success_count++;
	                sample_counter++;

	                /*
	                 * Keep the latest values for debugger inspection.
	                 */
	                scg_accel_x_raw = accel_sample.x;
	                scg_accel_y_raw = accel_sample.y;
	                scg_accel_z_raw = accel_sample.z;

	                /*
	                 * Build one complete queue message.
	                 */
	                sample.sample_counter = sample_counter;
	                sample.timestamp_ms = HAL_GetTick();

	                sample.accel_x_raw = accel_sample.x;
	                sample.accel_y_raw = accel_sample.y;
	                sample.accel_z_raw = accel_sample.z;
	                sample.reserved = 0;

	                /*
	                 * Send without blocking acquisition.
	                 */
	                scg_queue_last_status =
	                    tx_queue_send(
	                        &scg_sample_queue,
	                        &sample,
	                        TX_NO_WAIT);

	                if (scg_queue_last_status == TX_SUCCESS)
	                {
	                    scg_queue_send_count++;
	                }
	                else
	                {
	                    scg_queue_drop_count++;
	                }
	            }
	            else
	            {
	                scg_spi_error_count++;
	            }
	        }
	    }
}





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

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ICM_INT_Pin)
    {
        /*
         * Do not access the semaphore before it has been created.
         */
        if (scg_sync_ready != 0U)
        {
            scg_semaphore_last_status =
                tx_semaphore_put(&scg_drdy_semaphore);

            if (scg_semaphore_last_status == TX_SUCCESS)
            {
                scg_semaphore_put_success_count++;
            }
            else
            {
                scg_semaphore_put_error_count++;
            }
        }
    }
}
/* USER CODE END 1 */
