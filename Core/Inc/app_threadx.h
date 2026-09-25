/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.h
  * @author  MCD Application Team
  * @brief   ThreadX applicative header file
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __APP_THREADX_H
#define __APP_THREADX_H
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "tx_api.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
typedef struct
{
    uint32_t sample_counter;

    /*
     * ECG Data Ready interrupt time in microseconds.
     * Generated from the common 1 MHz TIM2 timer.
     */
    uint64_t timestamp_us;

    int32_t ch1_raw;
    int32_t ch2_raw;

    int32_t ch2_bandpass;
    int32_t ch2_notch;
    int32_t ch2_bandpass_notch;
    int32_t ch2_all_filter;
} ECG_Sample;

typedef struct
{
    uint32_t sample_counter;

    /*
     * SCG Data Ready interrupt time in microseconds.
     * Generated from the same TIM2 timer used by ECG.
     */
    uint64_t timestamp_us;

    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;

    int16_t reserved;
} SCG_Sample;

/*
 * Timestamp captured at the ECG or SCG Data Ready interrupt.
 *
 * timestamp_low:
 *   Lower 32 bits from the 1 MHz TIM2 counter.
 *
 * timestamp_high:
 *   Upper 32 bits incremented whenever TIM2 wraps.
 */
typedef struct
{
    uint32_t timestamp_low;
    uint32_t timestamp_high;
} SensorIrqTimestamp;

/* Compact, naturally 32-bit aligned acquisition-to-detector message. */
typedef struct
{
    uint32_t sample_counter;
    uint32_t timestamp_low;
    uint32_t timestamp_high;
    int32_t ecg_raw;
} ECG_ProcessingSample;

/* USB record type "R".  RR is microseconds and confidence is Q15. */
typedef struct
{
    uint32_t sample_counter;
    uint32_t timestamp_low;
    uint32_t timestamp_high;
    int32_t amplitude;
    uint32_t rr_interval_us;
    uint32_t heart_rate_bpm;
    uint32_t confidence_q15;
    uint32_t adaptive_threshold;
} ECG_RPeakEvent;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Private defines -----------------------------------------------------------*/
#define TX_APP_STACK_SIZE                       1024
#define TX_APP_THREAD_PRIO                      5

/* USER CODE BEGIN PD */
#define ECG_QUEUE_CAPACITY       128U
#define ECG_QUEUE_MESSAGE_SIZE   10U

#define SCG_QUEUE_CAPACITY       128U
#define SCG_QUEUE_MESSAGE_SIZE   6U

/*
 * ECG and SCG interrupt timestamp queues.
 */
#define TIMESTAMP_QUEUE_CAPACITY       128U
#define TIMESTAMP_QUEUE_MESSAGE_SIZE   2U

#define SCG_THREAD_STACK_SIZE    1024U
#define SCG_THREAD_PRIORITY      5U

/* ADS1292R CONFIG1=0x02 selects 500 samples/s. */
#define ECG_SAMPLE_RATE_HZ                 500U
#define ECG_PROCESSING_QUEUE_CAPACITY      128U
#define ECG_PROCESSING_QUEUE_MESSAGE_SIZE  4U
#define ECG_RPEAK_QUEUE_CAPACITY           32U
#define ECG_RPEAK_QUEUE_MESSAGE_SIZE       8U
#define ECG_PROCESSING_THREAD_STACK_SIZE   2048U
#define ECG_PROCESSING_THREAD_PRIORITY     6U

/* Integer Pan-Tompkins timing parameters (all derived from fs). */
#define ECG_PT_BASELINE_WINDOW_SAMPLES ((ECG_SAMPLE_RATE_HZ * 200U) / 1000U)
#define ECG_PT_LOWPASS_WINDOW_SAMPLES  ((ECG_SAMPLE_RATE_HZ * 24U) / 1000U)
#define ECG_PT_MWI_WINDOW_SAMPLES      ((ECG_SAMPLE_RATE_HZ * 150U) / 1000U)
#define ECG_PT_REFRACTORY_SAMPLES      ((ECG_SAMPLE_RATE_HZ * 200U) / 1000U)
/*
 * The high-pass stage is x[n] - moving_average(x[n]); its direct path means
 * that adding half of the baseline window to a group-delay estimate is
 * incorrect.  Refine each integrated-domain candidate against the original
 * ECG in this causal look-back interval instead.
 */
#define ECG_PT_RPEAK_LOOKBACK_SAMPLES  ((ECG_SAMPLE_RATE_HZ * 200U) / 1000U)
/* USER CODE END PD */

/* Main thread defines -------------------------------------------------------*/
#ifndef TX_APP_THREAD_PREEMPTION_THRESHOLD
#define TX_APP_THREAD_PREEMPTION_THRESHOLD      TX_APP_THREAD_PRIO
#endif

#ifndef TX_APP_THREAD_TIME_SLICE
#define TX_APP_THREAD_TIME_SLICE                TX_NO_TIME_SLICE
#endif

#ifndef TX_APP_THREAD_AUTO_START
#define TX_APP_THREAD_AUTO_START                TX_AUTO_START
#endif
/* USER CODE BEGIN MTD */

/* USER CODE END MTD */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
UINT App_ThreadX_Init(VOID *memory_ptr);
void MX_ThreadX_Init(void);
void ecg_acquisition_thread_entry(ULONG thread_input);

/* USER CODE BEGIN EFP */
extern TX_QUEUE ecg_sample_queue;
extern TX_QUEUE scg_sample_queue;
extern TX_QUEUE ecg_processing_queue;
extern TX_QUEUE ecg_rpeak_queue;

void scg_acquisition_thread_entry(ULONG thread_input);
void ecg_processing_thread_entry(ULONG thread_input);
/* USER CODE END EFP */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_THREADX_H */
