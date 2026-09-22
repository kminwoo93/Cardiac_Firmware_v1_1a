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
/* USER CODE BEGIN PV */
TX_QUEUE ecg_sample_queue;
TX_QUEUE scg_sample_queue;
/*
 * Timestamp event queues written by the EXTI callbacks
 * and read by the acquisition threads.
 */
TX_QUEUE ecg_timestamp_queue;
TX_QUEUE scg_timestamp_queue;

static ULONG ecg_queue_storage[
    ECG_QUEUE_CAPACITY * ECG_QUEUE_MESSAGE_SIZE
];
static ULONG scg_queue_storage[
    SCG_QUEUE_CAPACITY * SCG_QUEUE_MESSAGE_SIZE
];
/*
 * Static storage for timestamp queues.
 *
 * Each timestamp queue contains 128 messages.
 * Each message is 2 ULONG = 8 bytes.
 */
static ULONG ecg_timestamp_queue_storage[
    TIMESTAMP_QUEUE_CAPACITY *
    TIMESTAMP_QUEUE_MESSAGE_SIZE
];

static ULONG scg_timestamp_queue_storage[
    TIMESTAMP_QUEUE_CAPACITY *
    TIMESTAMP_QUEUE_MESSAGE_SIZE
];

extern volatile uint32_t drdy_irq_count;
volatile uint32_t ecg_thread_wakeup_count = 0;
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
 * Prevent EXTI callbacks from accessing timestamp queues
 * before tx_queue_create() has completed.
 */
volatile uint8_t ecg_timestamp_queue_ready = 0U;
volatile uint8_t scg_timestamp_queue_ready = 0U;
/*
 * Defined in stm32u5xx_it.c.
 */
extern volatile uint32_t icm_drdy_irq_count;

/*
 * Diagnostic counters.
 */
volatile uint32_t scg_thread_wakeup_count = 0U;

volatile uint32_t scg_spi_success_count = 0U;
volatile uint32_t scg_spi_error_count = 0U;

/*
 * Most recent return values.
 */
volatile HAL_StatusTypeDef scg_spi_last_status = HAL_OK;

/*
 * Most recently acquired raw accelerometer values.
 */
volatile int16_t scg_accel_x_raw = 0;
volatile int16_t scg_accel_y_raw = 0;
volatile int16_t scg_accel_z_raw = 0;

/*
 * ECG timestamp queue diagnostics.
 */
volatile uint32_t ecg_timestamp_queue_send_count = 0U;
volatile uint32_t ecg_timestamp_queue_drop_count = 0U;
volatile UINT ecg_timestamp_queue_last_status = TX_SUCCESS;

/*
 * SCG timestamp queue diagnostics.
 */
volatile uint32_t scg_timestamp_queue_send_count = 0U;
volatile uint32_t scg_timestamp_queue_drop_count = 0U;
volatile UINT scg_timestamp_queue_last_status = TX_SUCCESS;

/*
 * Timestamp queue receive diagnostics.
 */
volatile uint32_t ecg_timestamp_queue_receive_count = 0U;
volatile uint32_t ecg_timestamp_queue_receive_error_count = 0U;
volatile UINT ecg_timestamp_queue_receive_last_status = TX_SUCCESS;

volatile uint32_t scg_timestamp_queue_receive_count = 0U;
volatile uint32_t scg_timestamp_queue_receive_error_count = 0U;
volatile UINT scg_timestamp_queue_receive_last_status = TX_SUCCESS;
/*
 * Most recently received ISR timestamp values.
 * These are exposed for Live Expressions.
 */
volatile uint32_t ecg_irq_timestamp_high = 0U;
volatile uint32_t ecg_irq_timestamp_low = 0U;

volatile uint32_t scg_irq_timestamp_high = 0U;
volatile uint32_t scg_irq_timestamp_low = 0U;
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
   * Timestamp queue messages must be exactly 2 ULONG
   * units, which is 8 bytes on this MCU.
   */
  if (sizeof(SensorIrqTimestamp) !=
      (TIMESTAMP_QUEUE_MESSAGE_SIZE * sizeof(ULONG)))
  {
      return TX_SIZE_ERROR;
  }

  /*
   * Do not allow EXTI callbacks to access the queues
   * while they are being created.
   */
  ecg_timestamp_queue_ready = 0U;
  scg_timestamp_queue_ready = 0U;

  /*
   * Create ECG interrupt timestamp queue.
   */
  if (tx_queue_create(
          &ecg_timestamp_queue,
          "ecg_timestamp_queue",
          TIMESTAMP_QUEUE_MESSAGE_SIZE,
          ecg_timestamp_queue_storage,
          sizeof(ecg_timestamp_queue_storage)) != TX_SUCCESS)
  {
      return TX_QUEUE_ERROR;
  }

  /*
   * Create SCG interrupt timestamp queue.
   */
  if (tx_queue_create(
          &scg_timestamp_queue,
          "scg_timestamp_queue",
          TIMESTAMP_QUEUE_MESSAGE_SIZE,
          scg_timestamp_queue_storage,
          sizeof(scg_timestamp_queue_storage)) != TX_SUCCESS)
  {
      return TX_QUEUE_ERROR;
  }

  /*
   * Reset timestamp queue diagnostic counters.
   */
  ecg_timestamp_queue_send_count = 0U;
  ecg_timestamp_queue_drop_count = 0U;
  ecg_timestamp_queue_last_status = TX_SUCCESS;

  scg_timestamp_queue_send_count = 0U;
  scg_timestamp_queue_drop_count = 0U;
  scg_timestamp_queue_last_status = TX_SUCCESS;

  ecg_irq_timestamp_high = 0U;
  ecg_irq_timestamp_low = 0U;

  scg_irq_timestamp_high = 0U;
  scg_irq_timestamp_low = 0U;
  /*
   * Timestamp queues may now be accessed from EXTI callbacks.
   */
  ecg_timestamp_queue_ready = 1U;
  scg_timestamp_queue_ready = 1U;

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

  /* Remove interrupts that occurred before the thread was ready. */
  __HAL_GPIO_EXTI_CLEAR_IT(ICM_INT_Pin);
  NVIC_ClearPendingIRQ(ICM_INT_EXTI_IRQn);

  /*
   * Start all diagnostic counters from the same point.
   */
  icm_drdy_irq_count = 0U;
  scg_thread_wakeup_count = 0U;
  scg_spi_success_count = 0U;
  scg_spi_error_count = 0U;

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
	SensorIrqTimestamp irq_timestamp;

	ADS1292R_CH2FilterState ch2_filter;
	ADS1292R_CH2FilterOutput ch2_output;

	uint32_t sample_counter = 0;
	TX_PARAMETER_NOT_USED(thread_input);

	uint8_t ecg_raw[9];


	drdy_irq_count = 0;
	ecg_thread_wakeup_count = 0;

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
		    /*
		     * Wait directly for one ECG interrupt timestamp.
		     *
		     * TX_WAIT_FOREVER blocks this thread without consuming CPU.
		     * One queue message represents one ECG Data Ready event.
		     */
		    ecg_timestamp_queue_receive_last_status =
		        tx_queue_receive(
		            &ecg_timestamp_queue,
		            &irq_timestamp,
		            TX_WAIT_FOREVER);

		    if (ecg_timestamp_queue_receive_last_status == TX_SUCCESS)
		    {
		        ecg_timestamp_queue_receive_count++;
		        ecg_thread_wakeup_count++;

		        /*
		         * Expose the most recently received timestamp
		         * through Live Expressions.
		         */
		        ecg_irq_timestamp_high =
		            irq_timestamp.timestamp_high;

		        ecg_irq_timestamp_low =
		            irq_timestamp.timestamp_low;

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
		    	        /*
		    	         * Combine the high and low 32-bit timer words into
		    	         * one continuous 64-bit microsecond timestamp.
		    	         */
		    	        sample.timestamp_us =
		    	            ((uint64_t)irq_timestamp.timestamp_high << 32) |
		    	            (uint64_t)irq_timestamp.timestamp_low;

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
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

/*
 * SCG acquisition thread.
 *
 * This function is placed inside USER CODE BEGIN 1 so that
 * CubeMX code generation does not delete it.
 */
void scg_acquisition_thread_entry(ULONG thread_input)
{
	ICM20948_AccelRaw accel_sample;
	SCG_Sample sample;
	SensorIrqTimestamp irq_timestamp;

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
        /*
         * Wait directly for one SCG interrupt timestamp.
         *
         * One timestamp queue message represents one
         * ICM-20948 Data Ready event.
         */
        scg_timestamp_queue_receive_last_status =
            tx_queue_receive(
                &scg_timestamp_queue,
                &irq_timestamp,
                TX_WAIT_FOREVER);

        if (scg_timestamp_queue_receive_last_status == TX_SUCCESS)
        {
            scg_timestamp_queue_receive_count++;
            scg_thread_wakeup_count++;

            /*
             * Expose the most recently received timestamp
             * through Live Expressions.
             */
            scg_irq_timestamp_high =
                irq_timestamp.timestamp_high;

            scg_irq_timestamp_low =
                irq_timestamp.timestamp_low;

            scg_spi_last_status =
                ICM20948_ReadAccelRaw(&accel_sample);

            if (scg_spi_last_status == HAL_OK)
            {
                scg_spi_success_count++;
                sample_counter++;

                /*
                 * Keep the latest values for Live Expressions.
                 */
                scg_accel_x_raw = accel_sample.x;
                scg_accel_y_raw = accel_sample.y;
                scg_accel_z_raw = accel_sample.z;

                /* Build one SCG sample record. */
                sample.sample_counter = sample_counter;
                /*
                 * Combine the high and low 32-bit timer words into
                 * one continuous 64-bit microsecond timestamp.
                 */
                sample.timestamp_us =
                    ((uint64_t)irq_timestamp.timestamp_high << 32) |
                    (uint64_t)irq_timestamp.timestamp_low;

                sample.accel_x_raw = accel_sample.x;
                sample.accel_y_raw = accel_sample.y;
                sample.accel_z_raw = accel_sample.z;
                sample.reserved = 0;

                /*
                 * Do not block the acquisition thread when
                 * the USB sample queue is full.
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
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == DRDY_Pin)
    {

        /*
         * Capture and queue the exact ECG Data Ready
         * interrupt time before waking the ECG thread.
         */
        if (ecg_timestamp_queue_ready != 0U)
        {
            SensorIrqTimestamp irq_timestamp;

            Timestamp_CaptureFromISR(
                &irq_timestamp.timestamp_high,
                &irq_timestamp.timestamp_low);

            ecg_timestamp_queue_last_status =
                tx_queue_send(
                    &ecg_timestamp_queue,
                    &irq_timestamp,
                    TX_NO_WAIT);

            if (ecg_timestamp_queue_last_status == TX_SUCCESS)
            {
                ecg_timestamp_queue_send_count++;
            }
            else
            {
                ecg_timestamp_queue_drop_count++;
            }
        }
    }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ICM_INT_Pin)
    {
        /*
         * Do not access the ThreadX queue before
         * tx_queue_create() has completed.
         */
        if (scg_timestamp_queue_ready != 0U)
        {
            SensorIrqTimestamp irq_timestamp;

            /*
             * Capture the exact SCG Data Ready time.
             * Sending the timestamp also wakes the SCG thread.
             */
            Timestamp_CaptureFromISR(
                &irq_timestamp.timestamp_high,
                &irq_timestamp.timestamp_low);

            scg_timestamp_queue_last_status =
                tx_queue_send(
                    &scg_timestamp_queue,
                    &irq_timestamp,
                    TX_NO_WAIT);

            if (scg_timestamp_queue_last_status == TX_SUCCESS)
            {
                scg_timestamp_queue_send_count++;
            }
            else
            {
                scg_timestamp_queue_drop_count++;
            }
        }
    }
}


/* USER CODE END 1 */
