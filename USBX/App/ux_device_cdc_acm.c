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
#include "ads1292r.h"
#include "ads1292r_acquisition.h"
#include "main.h"
#include <inttypes.h>
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
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
UX_SLAVE_CLASS_CDC_ACM * volatile g_cdc_acm = UX_NULL;
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
	(void)ux_device_class_cdc_acm_ioctl(
	    g_cdc_acm, UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_WRITE_TIMEOUT,
	    (VOID *)(ALIGN_TYPE)UX_MS_TO_TICK(100U));
	ADS1292R_USB_Activated();
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
	ADS1292R_USB_Deactivated();
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
    static const UCHAR csv_header[] =
        "sample_counter,timestamp_us,status_hex,ch1_raw,ch2_raw,ch2_filtered\r\n";
    ADS1292R_Record batch[ADS1292R_USB_BATCH_SIZE];
    UCHAR usb_buffer[1024];
    UX_SLAVE_CLASS_CDC_ACM *cdc;
    ULONG actual_length;
    size_t used;
    uint32_t started;
    uint32_t elapsed;
    uint32_t i;
    UINT status;
    int length;
    uint8_t header_sent = 0U;

    TX_PARAMETER_NOT_USED(thread_input);

    for (;;)
    {
        cdc = g_cdc_acm;
        if ((cdc == UX_NULL) ||
            (_ux_system_slave->ux_system_slave_device
                 .ux_slave_device_state != UX_DEVICE_CONFIGURED))
        {
            header_sent = 0U;
            tx_thread_sleep(10U);
            continue;
        }

        if (header_sent == 0U)
        {
            actual_length = 0U;
            started = ADS1292R_ProfileNow();
            status = ux_device_class_cdc_acm_write(
                cdc, (UCHAR *)csv_header,
                (ULONG)(sizeof(csv_header) - 1U), &actual_length);
            elapsed = ADS1292R_ProfileNow() - started;
            ads_usb_write_us_last = elapsed;
            if (elapsed > ads_usb_write_us_max)
            {
                ads_usb_write_us_max = elapsed;
            }
            if ((status != UX_SUCCESS) ||
                (actual_length != (ULONG)(sizeof(csv_header) - 1U)))
            {
                ads_usb_error_count++;
                header_sent = 0U;
                tx_thread_sleep(10U);
                continue;
            }
            header_sent = 1U;
        }

        /* This call only copies ten records while interrupts are briefly
         * masked.  Formatting and USB transfer occur after the lock is gone. */
        (void)ADS1292R_WaitAndPopBatch(batch);

        cdc = g_cdc_acm;
        if ((cdc == UX_NULL) ||
            (_ux_system_slave->ux_system_slave_device
                 .ux_slave_device_state != UX_DEVICE_CONFIGURED))
        {
            header_sent = 0U;
            continue;
        }

        started = ADS1292R_ProfileNow();
        used = 0U;
        for (i = 0U; i < ADS1292R_USB_BATCH_SIZE; ++i)
        {
            length = snprintf((char *)&usb_buffer[used],
                              sizeof(usb_buffer) - used,
                              "%" PRIu32 ",%" PRIu64 ",%06" PRIX32
                              ",%" PRId32 ",%" PRId32 ",%.6f\r\n",
                              batch[i].sample_counter,
                              batch[i].timestamp_us,
                              batch[i].status,
                              batch[i].ch1_raw,
                              batch[i].ch2_raw,
                              (double)batch[i].ch2_filtered);
            if ((length < 0) ||
                ((size_t)length >= (sizeof(usb_buffer) - used)))
            {
                ads_usb_error_count++;
                used = 0U;
                break;
            }
            used += (size_t)length;
        }
        elapsed = ADS1292R_ProfileNow() - started;
        ads_usb_format_us_last = elapsed;
        if (elapsed > ads_usb_format_us_max)
        {
            ads_usb_format_us_max = elapsed;
        }
        if (used == 0U)
        {
            continue;
        }

        actual_length = 0U;
        started = ADS1292R_ProfileNow();
        status = ux_device_class_cdc_acm_write(
            cdc, usb_buffer, (ULONG)used, &actual_length);
        elapsed = ADS1292R_ProfileNow() - started;
        ads_usb_write_us_last = elapsed;
        if (elapsed > ads_usb_write_us_max)
        {
            ads_usb_write_us_max = elapsed;
        }
        if ((status != UX_SUCCESS) || (actual_length != (ULONG)used))
        {
            ads_usb_error_count++;
            header_sent = 0U;
        }
    }
}


/* USER CODE END 1 */
