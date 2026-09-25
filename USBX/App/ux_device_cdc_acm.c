/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ux_device_cdc_acm.c
  * @author  MCD Application Team
  * @brief   USBX Device applicative file
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
#include "ux_device_cdc_acm.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_threadx.h"
#include "main.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define USB_BATCH_ECG_SAMPLES    16U
#define SCG_USB_BATCH_SAMPLES    20U
#define USB_BATCH_BUFFER_SIZE    4096U
/*
 * Do not remove a queue message unless enough buffer
 * space remains for one complete CSV row.
 */
#define USB_CSV_ROW_RESERVE      160U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static UCHAR usb_batch_buffer[USB_BATCH_BUFFER_SIZE];

volatile uint32_t usb_queue_receive_count = 0;
volatile uint32_t usb_sample_send_count = 0;
volatile uint32_t usb_batch_send_count = 0;
volatile uint32_t usb_sample_error_count = 0;
volatile uint32_t usb_batch_error_count = 0;

volatile UINT usb_last_queue_status = TX_SUCCESS;
volatile UINT usb_last_write_status = UX_SUCCESS;

volatile uint32_t usb_scg_queue_receive_count = 0U;
volatile uint32_t usb_scg_queue_empty_count = 0U;

volatile UINT usb_scg_last_queue_status = TX_SUCCESS;

volatile uint32_t usb_scg_last_sample_counter = 0U;
volatile uint64_t usb_scg_last_timestamp_us = 0ULL;

volatile int16_t usb_scg_last_x_raw = 0;
volatile int16_t usb_scg_last_y_raw = 0;
volatile int16_t usb_scg_last_z_raw = 0;

volatile uint32_t usb_ecg_batch_record_count = 0U;
volatile uint32_t usb_scg_batch_record_count = 0U;
volatile uint32_t usb_total_record_send_count = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static uint32_t USB_UInt64ToDecimal(
    uint64_t value,
    char *destination,
    uint32_t destination_size);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
UX_SLAVE_CLASS_CDC_ACM *g_cdc_acm = UX_NULL;
/* USER CODE END 0 */

/**
  * @brief  USBD_CDC_ACM_Activate
  *         This function is called when insertion of a CDC ACM device.
  * @param  cdc_acm_instance: Pointer to the cdc acm class instance.
  * @retval none
  */
VOID USBD_CDC_ACM_Activate(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_Activate */
	g_cdc_acm = (UX_SLAVE_CLASS_CDC_ACM *)cdc_acm_instance;
  /* USER CODE END USBD_CDC_ACM_Activate */

  return;
}

/**
  * @brief  USBD_CDC_ACM_Deactivate
  *         This function is called when extraction of a CDC ACM device.
  * @param  cdc_acm_instance: Pointer to the cdc acm class instance.
  * @retval none
  */
VOID USBD_CDC_ACM_Deactivate(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_Deactivate */
	UX_PARAMETER_NOT_USED(cdc_acm_instance);
	  g_cdc_acm = UX_NULL;
  /* USER CODE END USBD_CDC_ACM_Deactivate */

  return;
}

/**
  * @brief  USBD_CDC_ACM_ParameterChange
  *         This function is invoked to manage the CDC ACM class requests.
  * @param  cdc_acm_instance: Pointer to the cdc acm class instance.
  * @retval none
  */
VOID USBD_CDC_ACM_ParameterChange(VOID *cdc_acm_instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_ParameterChange */
  UX_PARAMETER_NOT_USED(cdc_acm_instance);
  /* USER CODE END USBD_CDC_ACM_ParameterChange */

  return;
}

/* USER CODE BEGIN 1 */

/*
 * Convert one unsigned 64-bit integer into a decimal string.
 *
 * This avoids using %llu because the size-optimized newlib-nano
 * snprintf implementation may not support long-long formatting.
 *
 * Return value:
 *   Greater than zero: number of decimal characters written
 *   Zero: destination buffer was too small or invalid
 */
static uint32_t USB_UInt64ToDecimal(
    uint64_t value,
    char *destination,
    uint32_t destination_size)
{
    char reverse_digits[20];
    uint32_t digit_count = 0U;
    uint32_t index;

    if ((destination == NULL) || (destination_size == 0U))
    {
        return 0U;
    }

    /*
     * Generate digits in reverse order.
     *
     * The do-while form ensures that value 0
     * becomes the string "0".
     */
    do
    {
        reverse_digits[digit_count] =
            (char)('0' + (value % 10ULL));

        digit_count++;
        value /= 10ULL;
    }
    while ((value != 0ULL) &&
           (digit_count < sizeof(reverse_digits)));

    /*
     * One extra byte is required for the terminating '\0'.
     */
    if (destination_size <= digit_count)
    {
        destination[0] = '\0';
        return 0U;
    }

    /*
     * Reverse the generated digits into normal order.
     */
    for (index = 0U; index < digit_count; index++)
    {
        destination[index] =
            reverse_digits[digit_count - 1U - index];
    }

    destination[digit_count] = '\0';

    return digit_count;
}

VOID usbx_cdc_acm_read_thread_entry(ULONG thread_input)
{
    UCHAR rx_buffer[64];
    ULONG actual_length;
    UINT status;

    TX_PARAMETER_NOT_USED(thread_input);

    while (1)
    {
        if (g_cdc_acm != UX_NULL)
        {
            status = ux_device_class_cdc_acm_read(
                g_cdc_acm,
                rx_buffer,
                sizeof(rx_buffer),
                &actual_length);

            if (status == UX_SUCCESS)
            {
                /* Data received from PC */
                /* We will process it later */
            }
        }
        else
        {
            tx_thread_sleep(10);
        }
    }
}

VOID usbx_cdc_acm_write_thread_entry(ULONG thread_input)
{
	ECG_Sample sample;
	ECG_RPeakEvent rpeak_event;
	/*
	     * One SCG message received from scg_sample_queue.
	     */
	    SCG_Sample scg_sample;
	    ULONG actual_length;
	    UINT status;

	    uint32_t batch_ecg_count;
	    uint32_t batch_scg_count;
	    uint32_t batch_record_count;
	    uint32_t batch_length;

	    int line_length;

	    char timestamp_text[21];

	    uint8_t stream_started = 0U;

	    TX_PARAMETER_NOT_USED(thread_input);

	    usb_queue_receive_count = 0;
	    usb_sample_send_count = 0;
	    usb_batch_send_count = 0;
	    usb_sample_error_count = 0;
	    usb_batch_error_count = 0;

	    usb_last_queue_status = TX_SUCCESS;
	    usb_last_write_status = UX_SUCCESS;

	    usb_scg_queue_receive_count = 0U;
	    usb_scg_queue_empty_count = 0U;

	    usb_scg_last_queue_status = TX_SUCCESS;

	    usb_scg_last_sample_counter = 0U;
	    usb_scg_last_timestamp_us = 0ULL;

	    usb_scg_last_x_raw = 0;
	    usb_scg_last_y_raw = 0;
	    usb_scg_last_z_raw = 0;

	    while (1)
	    {
	        /*
	         * Wait until USB CDC is connected and configured.
	         */
	        if ((g_cdc_acm == UX_NULL) ||
	            (_ux_system_slave->ux_system_slave_device
	                 .ux_slave_device_state != UX_DEVICE_CONFIGURED))
	        {
	            stream_started = 0U;

	            tx_thread_sleep(10);
	            continue;
	        }

	        /*
	         * Send CSV header once after USB connection.
	         */
	        if (stream_started == 0U)
	        {
	        	static const UCHAR csv_header[] =
	        	    "record_type,timestamp_us,sample_counter,v1,v2,v3\r\n";
	            actual_length = 0;

	            status = ux_device_class_cdc_acm_write(
	                g_cdc_acm,
	                (UCHAR *)csv_header,
	                (ULONG)(sizeof(csv_header) - 1U),
	                &actual_length);

	            usb_last_write_status = status;

	            if (status != UX_SUCCESS)
	            {
	                usb_batch_error_count++;

	                tx_thread_sleep(10);
	                continue;
	            }

	            stream_started = 1U;
	        }

	        /*
	         * Start building a new USB batch.
	         */
	        batch_ecg_count = 0U;
	        batch_scg_count = 0U;
	        batch_record_count = 0U;
	        batch_length = 0U;

	        while (batch_ecg_count < USB_BATCH_ECG_SAMPLES)
	        {
	            /*
	             * Do not remove an ECG sample until its complete CSV row is
	             * guaranteed space in this batch.
	             */
	            if ((USB_BATCH_BUFFER_SIZE - batch_length) <
	                USB_CSV_ROW_RESERVE)
	            {
	                break;
	            }

	            /*
	             * Sleep until the next ECG sample is available.
	             */
	            status = tx_queue_receive(
	                &ecg_sample_queue,
	                &sample,
	                TX_WAIT_FOREVER);

	            usb_last_queue_status = status;

	            if (status != TX_SUCCESS)
	            {
	                usb_sample_error_count++;
	                break;
	            }

	            usb_queue_receive_count++;

	            /*
	             * Append the received ECG record before draining SCG so the
	             * ECG sample cannot be displaced by SCG rows.
	             */
	            /*
	             * Convert the 64-bit timestamp without relying on
	             * snprintf long-long support.
	             */
	            if (USB_UInt64ToDecimal(
	                    sample.timestamp_us,
	                    timestamp_text,
	                    sizeof(timestamp_text)) == 0U)
	            {
	                usb_sample_error_count++;
	                break;
	            }

	            line_length = snprintf(
	                (char *)&usb_batch_buffer[batch_length],
	                USB_BATCH_BUFFER_SIZE - batch_length,
	                "E,%s,%lu,%ld,0,0\r\n",
	                timestamp_text,
	                (unsigned long)sample.sample_counter,
	                (long)sample.ch2_raw);

	            if ((line_length <= 0) ||
	                ((uint32_t)line_length >=
	                 (USB_BATCH_BUFFER_SIZE - batch_length)))
	            {
	                usb_sample_error_count++;
	                break;
	            }

	            batch_length += (uint32_t)line_length;
	            batch_ecg_count++;
	            batch_record_count++;

                /*
                 * Drain pending detector events without ever blocking the
                 * processing thread.  R records are:
                 * R,timestamp_us,sample_counter,amplitude,rr_us,bpm,
                 * confidence_q15,adaptive_threshold
                 */
                while ((USB_BATCH_BUFFER_SIZE - batch_length) >=
                       USB_CSV_ROW_RESERVE)
                {
                    if (tx_queue_receive(&ecg_rpeak_queue, &rpeak_event,
                                         TX_NO_WAIT) != TX_SUCCESS)
                    {
                        break;
                    }
                    uint64_t rpeak_timestamp =
                        ((uint64_t)rpeak_event.timestamp_high << 32) |
                        rpeak_event.timestamp_low;
                    if (USB_UInt64ToDecimal(rpeak_timestamp, timestamp_text,
                                            sizeof(timestamp_text)) == 0U)
                    {
                        usb_sample_error_count++;
                        break;
                    }
                    line_length = snprintf(
                        (char *)&usb_batch_buffer[batch_length],
                        USB_BATCH_BUFFER_SIZE - batch_length,
                        "R,%s,%lu,%ld,%lu,%lu,%lu,%lu\r\n",
                        timestamp_text,
                        (unsigned long)rpeak_event.sample_counter,
                        (long)rpeak_event.amplitude,
                        (unsigned long)rpeak_event.rr_interval_us,
                        (unsigned long)rpeak_event.heart_rate_bpm,
                        (unsigned long)rpeak_event.confidence_q15,
                        (unsigned long)rpeak_event.adaptive_threshold);
                    if ((line_length <= 0) ||
                        ((uint32_t)line_length >=
                         (USB_BATCH_BUFFER_SIZE - batch_length)))
                    {
                        usb_sample_error_count++;
                        break;
                    }
                    batch_length += (uint32_t)line_length;
                    batch_record_count++;
                }

	            /*
	             * Drain only the SCG samples that fit in this batch, up to the
	             * per-batch limit. Check both conditions before queue receive
	             * so unprocessed samples remain queued for the next batch.
	             *
	             * Each received SCG sample is immediately converted into one
	             * CSV "S" record and appended to usb_batch_buffer.
	             */
	            while ((batch_scg_count < SCG_USB_BATCH_SAMPLES) &&
	                   ((USB_BATCH_BUFFER_SIZE - batch_length) >=
	                    USB_CSV_ROW_RESERVE))
	            {
	                usb_scg_last_queue_status =
	                    tx_queue_receive(
	                        &scg_sample_queue,
	                        &scg_sample,
	                        TX_NO_WAIT);

	                /*
	                 * No more SCG samples are currently available.
	                 */
	                if (usb_scg_last_queue_status != TX_SUCCESS)
	                {
	                    if (usb_scg_last_queue_status == TX_QUEUE_EMPTY)
	                    {
	                        usb_scg_queue_empty_count++;
	                    }

	                    break;
	                }

	                /*
	                 * One SCG sample was successfully removed from the queue.
	                 */
	                usb_scg_queue_receive_count++;

	                /*
	                 * Save the latest SCG values for Live Expressions.
	                 */
	                usb_scg_last_sample_counter =
	                    scg_sample.sample_counter;

	                usb_scg_last_timestamp_us =
	                    scg_sample.timestamp_us;

	                usb_scg_last_x_raw =
	                    scg_sample.accel_x_raw;

	                usb_scg_last_y_raw =
	                    scg_sample.accel_y_raw;

	                usb_scg_last_z_raw =
	                    scg_sample.accel_z_raw;

	                /*
	                 * Append one SCG CSV record.
	                 *
	                 * ECG fields are zero because this is an SCG record.
	                 */
	                /*
	                 * Convert the 64-bit timestamp without relying on
	                 * snprintf long-long support.
	                 */
	                if (USB_UInt64ToDecimal(
	                        scg_sample.timestamp_us,
	                        timestamp_text,
	                        sizeof(timestamp_text)) == 0U)
	                {
	                    usb_sample_error_count++;
	                    break;
	                }

	                line_length = snprintf(
	                    (char *)&usb_batch_buffer[batch_length],
	                    USB_BATCH_BUFFER_SIZE - batch_length,
	                    "S,%s,%lu,%d,%d,%d\r\n",
	                    timestamp_text,
	                    (unsigned long)scg_sample.sample_counter,
	                    (int)scg_sample.accel_x_raw,
	                    (int)scg_sample.accel_y_raw,
	                    (int)scg_sample.accel_z_raw);
	                /*
	                 * Check whether snprintf succeeded and the row fitted.
	                 */
	                if ((line_length <= 0) ||
	                    ((uint32_t)line_length >=
	                     (USB_BATCH_BUFFER_SIZE - batch_length)))
	                {
	                    usb_sample_error_count++;
	                    break;
	                }

	                /*
	                 * Include this SCG row in the current USB batch.
	                 */
	                batch_length += (uint32_t)line_length;
	                batch_scg_count++;
	                batch_record_count++;
	            }
	        }

	        /*
	         * Do not attempt a zero-length USB transfer.
	         */
	        if (batch_record_count == 0U)
	        {
	            continue;
	        }

	        /*
	         * USB may have disconnected while collecting the batch.
	         */
	        if ((g_cdc_acm == UX_NULL) ||
	            (_ux_system_slave->ux_system_slave_device
	                 .ux_slave_device_state != UX_DEVICE_CONFIGURED))
	        {
	        	usb_sample_error_count += batch_record_count;
	            stream_started = 0U;
	            continue;
	        }

	        actual_length = 0;


	        /*
	         * Send the completed CSV batch with one USB write call.
	         */
	        status = ux_device_class_cdc_acm_write(
	            g_cdc_acm,
	            usb_batch_buffer,
	            (ULONG)batch_length,
	            &actual_length);


	        usb_last_write_status = status;

	        if ((status == UX_SUCCESS) &&
	            (actual_length == (ULONG)batch_length))
	        {
	            usb_batch_send_count++;
	            usb_sample_send_count += batch_record_count;
	        }
	        else
	        {
	            usb_batch_error_count++;
	            usb_sample_error_count += batch_record_count;

	            stream_started = 0U;
	            tx_thread_sleep(1);
	        }
	    }
}


/* USER CODE END 1 */
