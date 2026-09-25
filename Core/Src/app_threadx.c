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
TX_QUEUE ecg_processing_queue;
TX_QUEUE ecg_rpeak_queue;
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
static ULONG ecg_processing_queue_storage[
    ECG_PROCESSING_QUEUE_CAPACITY * ECG_PROCESSING_QUEUE_MESSAGE_SIZE];
static ULONG ecg_rpeak_queue_storage[
    ECG_RPEAK_QUEUE_CAPACITY * ECG_RPEAK_QUEUE_MESSAGE_SIZE];
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
TX_THREAD ecg_processing_thread;

volatile uint32_t ecg_processing_input_count = 0U;
volatile uint32_t ecg_rpeak_detected_count = 0U;
volatile uint32_t ecg_processing_queue_overflow_count = 0U;
volatile uint32_t ecg_rejected_peak_count = 0U;
volatile uint32_t ecg_searchback_detection_count = 0U;
volatile uint32_t ecg_processing_max_execution_us = 0U;
volatile uint32_t ecg_rpeak_queue_overflow_count = 0U;

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

  if ((sizeof(ECG_ProcessingSample) !=
       (ECG_PROCESSING_QUEUE_MESSAGE_SIZE * sizeof(ULONG))) ||
      (sizeof(ECG_RPeakEvent) !=
       (ECG_RPEAK_QUEUE_MESSAGE_SIZE * sizeof(ULONG))))
  {
      return TX_SIZE_ERROR;
  }

  if (tx_queue_create(&ecg_processing_queue, "ecg_processing_queue",
                      ECG_PROCESSING_QUEUE_MESSAGE_SIZE,
                      ecg_processing_queue_storage,
                      sizeof(ecg_processing_queue_storage)) != TX_SUCCESS)
  {
      return TX_QUEUE_ERROR;
  }

  if (tx_queue_create(&ecg_rpeak_queue, "ecg_rpeak_queue",
                      ECG_RPEAK_QUEUE_MESSAGE_SIZE,
                      ecg_rpeak_queue_storage,
                      sizeof(ecg_rpeak_queue_storage)) != TX_SUCCESS)
  {
      return TX_QUEUE_ERROR;
  }

  CHAR *processing_stack_pointer;
  if (tx_byte_allocate(byte_pool, (VOID **)&processing_stack_pointer,
                       ECG_PROCESSING_THREAD_STACK_SIZE,
                       TX_NO_WAIT) != TX_SUCCESS)
  {
      return TX_POOL_ERROR;
  }
  if (tx_thread_create(&ecg_processing_thread, "ecg_processing_thread",
                       ecg_processing_thread_entry, 0U,
                       processing_stack_pointer,
                       ECG_PROCESSING_THREAD_STACK_SIZE,
                       ECG_PROCESSING_THREAD_PRIORITY,
                       ECG_PROCESSING_THREAD_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
      return TX_THREAD_ERROR;
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
	ECG_ProcessingSample processing_sample;
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

                        processing_sample.sample_counter = sample_counter;
                        processing_sample.timestamp_low = irq_timestamp.timestamp_low;
                        processing_sample.timestamp_high = irq_timestamp.timestamp_high;
                        processing_sample.ecg_raw = ecg_ch2_raw;
                        if (tx_queue_send(&ecg_processing_queue,
                                          &processing_sample,
                                          TX_NO_WAIT) != TX_SUCCESS)
                        {
                            /* Drop newest: acquisition and USB streaming never wait. */
                            ecg_processing_queue_overflow_count++;
                        }

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

#define PT_HISTORY_SIZE 256U

typedef struct
{
    ECG_ProcessingSample sample;
    int32_t localization_value;
} PT_HistoryEntry;

static int32_t pt_baseline_buffer[ECG_PT_BASELINE_WINDOW_SAMPLES];
static int32_t pt_lowpass_buffer[ECG_PT_LOWPASS_WINDOW_SAMPLES];
static uint32_t pt_mwi_buffer[ECG_PT_MWI_WINDOW_SAMPLES];
static PT_HistoryEntry pt_history[PT_HISTORY_SIZE];

static uint32_t PT_Abs32(int32_t value)
{
    if (value >= 0)
    {
        return (uint32_t)value;
    }
    return (value == INT32_MIN) ? 0x7FFFFFFFU : (uint32_t)(-value);
}

static void PT_EmitPeak(uint32_t detection_counter,
                        uint32_t integrated_peak,
                        uint32_t threshold,
                        uint32_t *last_peak_counter,
                        uint64_t *last_peak_timestamp,
                        uint8_t searchback)
{
    /*
     * Do not subtract a summed "group delay" here.  The baseline-removal
     * filter contains an undelayed x[n] path, and the nonlinear square/MWI
     * peak has no fixed linear-phase delay.  Searching only already acquired
     * raw samples also guarantees that the emitted timestamp is an actual
     * ECG timestamp rather than a calculated approximation.
     */
    uint32_t first = (detection_counter > ECG_PT_RPEAK_LOOKBACK_SAMPLES) ?
        detection_counter - ECG_PT_RPEAK_LOOKBACK_SAMPLES : 1U;
    uint32_t last = detection_counter;
    uint32_t best_counter = detection_counter;
    uint32_t best_abs = 0U;
    ECG_ProcessingSample best = {0U, 0U, 0U, 0};
    ECG_RPeakEvent event;

    for (uint32_t counter = first; counter <= last; counter++)
    {
        PT_HistoryEntry *entry = &pt_history[counter % PT_HISTORY_SIZE];
        if (entry->sample.sample_counter == counter)
        {
            uint32_t magnitude = PT_Abs32(entry->localization_value);
            if (magnitude > best_abs)
            {
                best_abs = magnitude;
                best_counter = counter;
                best = entry->sample;
            }
        }
    }
    if ((best_abs == 0U) ||
        ((*last_peak_counter != 0U) &&
         ((best_counter - *last_peak_counter) < ECG_PT_REFRACTORY_SAMPLES)))
    {
        ecg_rejected_peak_count++;
        return;
    }

    uint64_t timestamp = ((uint64_t)best.timestamp_high << 32) |
                         (uint64_t)best.timestamp_low;
    uint64_t rr = (*last_peak_timestamp == 0ULL) ? 0ULL :
                  (timestamp - *last_peak_timestamp);
    event.sample_counter = best_counter;
    event.timestamp_low = best.timestamp_low;
    event.timestamp_high = best.timestamp_high;
    event.amplitude = best.ecg_raw;
    event.rr_interval_us = (rr > UINT32_MAX) ? UINT32_MAX : (uint32_t)rr;
    event.heart_rate_bpm = (rr == 0ULL) ? 0U : (uint32_t)(60000000ULL / rr);
    event.confidence_q15 = ((uint64_t)integrated_peak >=
                            ((uint64_t)threshold * 2ULL)) ? 32767U :
        (uint32_t)(((uint64_t)integrated_peak * 32767ULL) /
                   ((threshold == 0U) ? 1U : ((uint64_t)threshold * 2ULL)));
    event.adaptive_threshold = threshold;

    if (tx_queue_send(&ecg_rpeak_queue, &event, TX_NO_WAIT) != TX_SUCCESS)
    {
        ecg_rpeak_queue_overflow_count++;
    }
    ecg_rpeak_detected_count++;
    if (searchback != 0U)
    {
        ecg_searchback_detection_count++;
    }
    *last_peak_counter = best_counter;
    *last_peak_timestamp = timestamp;
}

void ecg_processing_thread_entry(ULONG thread_input)
{
    ECG_ProcessingSample input;
    int64_t baseline_sum = 0;
    int64_t lowpass_sum = 0;
    uint64_t mwi_sum = 0ULL;
    int32_t derivative_history[4] = {0, 0, 0, 0};
    uint32_t signal_level = 0U, noise_level = 0U, threshold = 0U;
    uint32_t previous_mwi = 0U, previous2_mwi = 0U;
    uint32_t last_peak_counter = 0U;
    uint64_t last_peak_timestamp = 0ULL;
    uint32_t rr_average = ECG_SAMPLE_RATE_HZ;
    uint32_t rejected_value = 0U, rejected_counter = 0U;
    TX_PARAMETER_NOT_USED(thread_input);

    while (1)
    {
        uint32_t start_high, start_low, end_high, end_low;
        if (tx_queue_receive(&ecg_processing_queue, &input,
                             TX_WAIT_FOREVER) != TX_SUCCESS)
        {
            continue;
        }
        Timestamp_CaptureFromISR(&start_high, &start_low);
        ecg_processing_input_count++;
        uint32_t bi = input.sample_counter % ECG_PT_BASELINE_WINDOW_SAMPLES;
        baseline_sum += input.ecg_raw - pt_baseline_buffer[bi];
        pt_baseline_buffer[bi] = input.ecg_raw;
        int32_t highpass = input.ecg_raw -
            (int32_t)(baseline_sum / (int32_t)ECG_PT_BASELINE_WINDOW_SAMPLES);

        uint32_t li = input.sample_counter % ECG_PT_LOWPASS_WINDOW_SAMPLES;
        lowpass_sum += highpass - pt_lowpass_buffer[li];
        pt_lowpass_buffer[li] = highpass;
        int32_t bandpass = (int32_t)(lowpass_sum /
                                    (int32_t)ECG_PT_LOWPASS_WINDOW_SAMPLES);
        pt_history[input.sample_counter % PT_HISTORY_SIZE].sample = input;
        pt_history[input.sample_counter % PT_HISTORY_SIZE].localization_value =
            highpass;

        int32_t derivative = (int32_t)(((2LL * bandpass) +
                              derivative_history[0] -
                              derivative_history[2] -
                              (2LL * derivative_history[3])) / 8LL);
        derivative_history[3] = derivative_history[2];
        derivative_history[2] = derivative_history[1];
        derivative_history[1] = derivative_history[0];
        derivative_history[0] = bandpass;
        int64_t scaled = derivative / 256;
        uint64_t squared64 = (uint64_t)(scaled * scaled);
        uint32_t squared = (squared64 > UINT32_MAX) ? UINT32_MAX :
                           (uint32_t)squared64;
        uint32_t mi = input.sample_counter % ECG_PT_MWI_WINDOW_SAMPLES;
        mwi_sum += squared;
        mwi_sum -= pt_mwi_buffer[mi];
        pt_mwi_buffer[mi] = squared;
        uint32_t mwi = (uint32_t)(mwi_sum / ECG_PT_MWI_WINDOW_SAMPLES);

        if ((previous_mwi > previous2_mwi) && (previous_mwi >= mwi) &&
            (input.sample_counter > ECG_PT_RPEAK_LOOKBACK_SAMPLES))
        {
            uint32_t candidate_counter = input.sample_counter - 1U;
            if ((threshold == 0U) || (previous_mwi >= threshold))
            {
                uint32_t old_peak = last_peak_counter;
                PT_EmitPeak(candidate_counter, previous_mwi, threshold,
                            &last_peak_counter, &last_peak_timestamp, 0U);
                signal_level = signal_level - (signal_level >> 3) +
                               (previous_mwi >> 3);
                if ((old_peak != 0U) && (last_peak_counter != old_peak))
                {
                    uint32_t current_rr = last_peak_counter - old_peak;
                    rr_average = rr_average - (rr_average >> 3) +
                                 (current_rr >> 3);
                }
                rejected_value = 0U;
            }
            else
            {
                noise_level = noise_level - (noise_level >> 3) +
                              (previous_mwi >> 3);
                ecg_rejected_peak_count++;
                if (previous_mwi > rejected_value)
                {
                    rejected_value = previous_mwi;
                    rejected_counter = candidate_counter;
                }
            }
            threshold = (signal_level > noise_level) ?
                noise_level + ((signal_level - noise_level) >> 2) :
                noise_level;
        }

        if ((last_peak_counter != 0U) && (rejected_value > (threshold >> 1)) &&
            ((input.sample_counter - last_peak_counter) >
             ((rr_average * 166U) / 100U)))
        {
            PT_EmitPeak(rejected_counter, rejected_value, threshold,
                        &last_peak_counter, &last_peak_timestamp, 1U);
            signal_level = signal_level - (signal_level >> 3) +
                           (rejected_value >> 3);
            rejected_value = 0U;
        }
        previous2_mwi = previous_mwi;
        previous_mwi = mwi;

        Timestamp_CaptureFromISR(&end_high, &end_low);
        uint64_t elapsed = (((uint64_t)end_high << 32) | end_low) -
                           (((uint64_t)start_high << 32) | start_low);
        if (elapsed > ecg_processing_max_execution_us)
        {
            ecg_processing_max_execution_us =
                (elapsed > UINT32_MAX) ? UINT32_MAX : (uint32_t)elapsed;
        }
    }
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
