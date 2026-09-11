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
#include "ads1292r_acquisition.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADS_ACQUISITION_STACK_SIZE  2048U
#define ADS_ACQUISITION_PRIORITY    5U
#define ADS_RING_CAPACITY           512U
#define ADS_USB_BATCH_SIZE          10U
#define ADS_BATCH_READY_FLAG        0x01U

_Static_assert(sizeof(ADS1292R_Record) == 40U,
               "Unexpected ADS1292R_Record ABI layout");

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern TIM_HandleTypeDef htim2;

volatile uint32_t ads_drdy_count;
volatile uint32_t ads_sample_count;
volatile uint32_t ads_overrun_count;
volatile uint32_t ads_bad_status_count;
volatile uint32_t ads_spi_error_count;
volatile uint32_t ads_buffer_overflow_count;
volatile uint32_t ads_usb_error_count;
volatile uint32_t ads_usb_reconnect_count;

volatile uint32_t ads_latency_us_last;
volatile uint32_t ads_latency_us_max;
volatile uint32_t ads_cs_duration_us_last;
volatile uint32_t ads_cs_duration_us_max;
volatile uint32_t ads_acquisition_us_last;
volatile uint32_t ads_acquisition_us_max;
volatile uint32_t ads_usb_format_us_last;
volatile uint32_t ads_usb_format_us_max;
volatile uint32_t ads_usb_write_us_last;
volatile uint32_t ads_usb_write_us_max;

static TX_THREAD ads_acquisition_thread;
static TX_SEMAPHORE ads_acquisition_semaphore;
static TX_EVENT_FLAGS_GROUP ads_buffer_events;
static ADS1292R_Record ads_ring[ADS_RING_CAPACITY];
static uint32_t ads_ring_head;
static uint32_t ads_ring_tail;
static uint32_t ads_ring_count;
static volatile uint8_t ads_runtime_ready;
static volatile uint8_t ads_request_pending;
static volatile uint8_t ads_acquisition_busy;
static uint32_t ads_pending_timer_count;
static uint64_t ads_pending_timestamp_us;
static uint32_t ads_pending_drdy_counter;
static uint32_t ads_timestamp_previous;
static uint64_t ads_timestamp_wrap_base;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID ADS1292R_AcquisitionThread(ULONG thread_input);
static void ADS1292R_RingPush(const ADS1292R_Record *record);

/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  /* USER CODE BEGIN App_ThreadX_MEM_POOL */

  /* USER CODE END App_ThreadX_MEM_POOL */
  /* USER CODE BEGIN App_ThreadX_Init */
  ret = ADS1292R_AcquisitionInit(memory_ptr);
  /* USER CODE END App_ThreadX_Init */

  return ret;
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

uint32_t ADS1292R_ProfileNow(void)
{
  return __HAL_TIM_GET_COUNTER(&htim2);
}

void ADS1292R_DRDY_EXTI_Callback(void)
{
  uint32_t now = ADS1292R_ProfileNow();
  uint64_t timestamp;

  ads_drdy_count++;

  /* TIM2 is a 1 MHz 32-bit free-running timer.  Comparing consecutive DRDY
   * captures extends it to 64 bits.  This relies on DRDY occurring much more
   * often than TIM2's 71.6 minute wrap.  Continuity is not guaranteed across
   * MCU reset/timer stop, or if DRDY is absent for 71.6 minutes or longer. */
  if (now < ads_timestamp_previous)
  {
    ads_timestamp_wrap_base += (1ULL << 32);
  }
  ads_timestamp_previous = now;
  timestamp = ads_timestamp_wrap_base | (uint64_t)now;

  if (ads_runtime_ready == 0U)
  {
    return;
  }

  if (ads_acquisition_busy != 0U)
  {
    /* ADS1292R has no FIFO: a DRDY arriving during the SPI/filter operation is
     * counted and dropped; it must not create a stale extra frame read. */
    ads_overrun_count++;
    return;
  }

  if (ads_request_pending != 0U)
  {
    /* Keep one token only.  Replace its metadata with the newest edge so that
     * the eventual read corresponds as closely as possible to current data. */
    ads_overrun_count++;
    ads_pending_timer_count = now;
    ads_pending_timestamp_us = timestamp;
    ads_pending_drdy_counter = ads_drdy_count;
    return;
  }

  ads_pending_timer_count = now;
  ads_pending_timestamp_us = timestamp;
  ads_pending_drdy_counter = ads_drdy_count;
  ads_request_pending = 1U;
  (void)tx_semaphore_put(&ads_acquisition_semaphore);
}

static void ADS1292R_RingPush(const ADS1292R_Record *record)
{
  uint8_t batch_ready;
  TX_INTERRUPT_SAVE_AREA

  TX_DISABLE
  if (ads_ring_count == ADS_RING_CAPACITY)
  {
    ads_ring_tail = (ads_ring_tail + 1U) % ADS_RING_CAPACITY;
    ads_ring_count--;
    ads_buffer_overflow_count++;
  }
  ads_ring[ads_ring_head] = *record;
  ads_ring_head = (ads_ring_head + 1U) % ADS_RING_CAPACITY;
  ads_ring_count++;
  batch_ready = (ads_ring_count >= ADS_USB_BATCH_SIZE) ? 1U : 0U;
  TX_RESTORE

  if (batch_ready != 0U)
  {
    (void)tx_event_flags_set(&ads_buffer_events, ADS_BATCH_READY_FLAG, TX_OR);
  }
}

static VOID ADS1292R_AcquisitionThread(ULONG thread_input)
{
  ADS1292R_CH2FilterState filter;
  ADS1292R_CH2FilterOutput filter_output;
  ADS1292R_Record record;
  uint8_t frame[9];
  uint32_t edge_timer;
  uint32_t started;
  uint32_t spi_started;
  uint32_t status_word;
  TX_INTERRUPT_SAVE_AREA

  TX_PARAMETER_NOT_USED(thread_input);
  ADS1292R_CH2FilterInit(&filter);

  for (;;)
  {
    (void)tx_semaphore_get(&ads_acquisition_semaphore, TX_WAIT_FOREVER);

    TX_DISABLE
    edge_timer = ads_pending_timer_count;
    record.timestamp_us = ads_pending_timestamp_us;
    record.drdy_counter = ads_pending_drdy_counter;
    ads_request_pending = 0U;
    ads_acquisition_busy = 1U;
    TX_RESTORE

    started = ADS1292R_ProfileNow();
    ads_latency_us_last = started - edge_timer;
    if (ads_latency_us_last > ads_latency_us_max)
    {
      ads_latency_us_max = ads_latency_us_last;
    }

    spi_started = ADS1292R_ProfileNow();
    if (ADS1292R_ReadData(frame) != HAL_OK)
    {
      ads_cs_duration_us_last = ADS1292R_ProfileNow() - spi_started;
      if (ads_cs_duration_us_last > ads_cs_duration_us_max)
      {
        ads_cs_duration_us_max = ads_cs_duration_us_last;
      }
      ads_spi_error_count++;
      goto acquisition_complete;
    }
    ads_cs_duration_us_last = ADS1292R_ProfileNow() - spi_started;
    if (ads_cs_duration_us_last > ads_cs_duration_us_max)
    {
      ads_cs_duration_us_max = ads_cs_duration_us_last;
    }

    status_word = ((uint32_t)frame[0] << 16) |
                  ((uint32_t)frame[1] << 8) | frame[2];
    if ((status_word & 0xF00000U) != 0xC00000U)
    {
      /* Invalid frames are diagnostic-only: do not advance the filter or
       * sample counter and do not publish a misleading ring record. */
      ads_bad_status_count++;
      goto acquisition_complete;
    }

    record.status = status_word;
    record.ch1_raw = ADS1292R_Convert24Bit(frame[3], frame[4], frame[5]);
    record.ch2_raw = ADS1292R_Convert24Bit(frame[6], frame[7], frame[8]);
    record.ch2_filtered = ADS1292R_ProcessCH2Sample(&filter,
                                                    record.ch2_raw,
                                                    &filter_output);
    record.error_flags = 0U;
    /* sample_counter counts only successfully read, status-valid records and
     * is incremented immediately before publishing that record. */
    record.sample_counter = ++ads_sample_count;
    ADS1292R_RingPush(&record);

acquisition_complete:
    ads_acquisition_us_last = ADS1292R_ProfileNow() - started;
    if (ads_acquisition_us_last > ads_acquisition_us_max)
    {
      ads_acquisition_us_max = ads_acquisition_us_last;
    }
    TX_DISABLE
    ads_acquisition_busy = 0U;
    TX_RESTORE
  }
}

void ADS1292R_USB_Activated(void)
{
  TX_INTERRUPT_SAVE_AREA

  TX_DISABLE
  /* Activation is the reconnect boundary.  Atomically discard every older
   * record; samples produced after this reset remain available to USB. */
  ads_ring_tail = ads_ring_head;
  ads_ring_count = 0U;
  ads_usb_reconnect_count++;
  TX_RESTORE
  (void)tx_event_flags_set(&ads_buffer_events, ADS_BATCH_READY_FLAG, TX_OR);
}

void ADS1292R_USB_Deactivated(void)
{
  (void)tx_event_flags_set(&ads_buffer_events, ADS_BATCH_READY_FLAG, TX_OR);
}

UINT ADS1292R_WaitAndPopBatch(ADS1292R_Record records[10])
{
  ULONG actual_flags;
  uint32_t i;
  uint8_t more_ready;
  TX_INTERRUPT_SAVE_AREA

  for (;;)
  {
    (void)tx_event_flags_get(&ads_buffer_events, ADS_BATCH_READY_FLAG,
                             TX_OR_CLEAR, &actual_flags, TX_WAIT_FOREVER);
    TX_DISABLE
    if (ads_ring_count >= ADS_USB_BATCH_SIZE)
    {
      for (i = 0U; i < ADS_USB_BATCH_SIZE; ++i)
      {
        records[i] = ads_ring[ads_ring_tail];
        ads_ring_tail = (ads_ring_tail + 1U) % ADS_RING_CAPACITY;
      }
      ads_ring_count -= ADS_USB_BATCH_SIZE;
      more_ready = (ads_ring_count >= ADS_USB_BATCH_SIZE) ? 1U : 0U;
      TX_RESTORE
      if (more_ready != 0U)
      {
        (void)tx_event_flags_set(&ads_buffer_events,
                                 ADS_BATCH_READY_FLAG, TX_OR);
      }
      return TX_SUCCESS;
    }
    TX_RESTORE
  }
}

/* USER CODE END 1 */
