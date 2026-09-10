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
#include "main.h"
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
    UCHAR usb_buffer[160];
    uint8_t ecg_raw[9];

    int32_t ch2_raw;
    ADS1292R_CH2FilterOutput ch2_output;
    int32_t ch2_bandpass_int;
    int32_t ch2_notch_int;
    int32_t ch2_bandpass_notch_int;
    int32_t ch2_all_filter_int;
    ADS1292R_CH2FilterState ch2_filter;

    uint32_t start_tick = 0;
    uint32_t timestamp_ms;
    uint32_t sample_counter = 0;

    ULONG actual_length;
    UINT status;
    int length;

    uint8_t stream_started = 0;

    GPIO_PinState previous_drdy = GPIO_PIN_SET;
    GPIO_PinState current_drdy;

    TX_PARAMETER_NOT_USED(thread_input);
    ADS1292R_CH2FilterInit(&ch2_filter);

    while (1)
    {
        /*
         * Check whether USB CDC is connected and configured.
         */
        if ((g_cdc_acm == UX_NULL) ||
            (_ux_system_slave->ux_system_slave_device
                 .ux_slave_device_state != UX_DEVICE_CONFIGURED))
        {
            stream_started = 0;
            previous_drdy = GPIO_PIN_SET;

            tx_thread_sleep(10);
            continue;
        }

        /*
         * Start a new stream after USB connection.
         */
        if (stream_started == 0)
        {
            static const UCHAR csv_header[] =
                "timestamp,sample_counter,ads1292r_raw,ch2_raw,"
                "ch2_bandpass,ch2_notch,ch2_bandpass_notch,ch2_all_filter\r\n";

            start_tick = HAL_GetTick();
            sample_counter = 0;
            ADS1292R_CH2FilterInit(&ch2_filter);
            previous_drdy =
                HAL_GPIO_ReadPin(DRDY_GPIO_Port, DRDY_Pin);

            actual_length = 0;

            status = ux_device_class_cdc_acm_write(
                g_cdc_acm,
                (UCHAR *)csv_header,
                (ULONG)(sizeof(csv_header) - 1U),
                &actual_length);

            if (status != UX_SUCCESS)
            {
                tx_thread_sleep(10);
                continue;
            }

            stream_started = 1;
        }

        /*
         * Read the current DRDY state.
         * ADS1292R DRDY is active-low.
         */
        current_drdy =
            HAL_GPIO_ReadPin(DRDY_GPIO_Port, DRDY_Pin);

        /*
         * Detect a DRDY falling edge:
         * previous HIGH and current LOW.
         */
        if ((previous_drdy == GPIO_PIN_SET) &&
            (current_drdy == GPIO_PIN_RESET))
        {
            /*
             * Save the current state before SPI and USB operations.
             */
            previous_drdy = GPIO_PIN_RESET;

            /*
             * Read one ADS1292R frame:
             * status[3] + CH1[3] + CH2[3].
             */
            ADS1292R_ReadData(ecg_raw);

            /*
             * Count every acquired frame, including a frame that later fails
             * the status check or USB transmission.  A gap in the host-side
             * sequence therefore exposes missing or rejected data.
             */
            sample_counter++;

            /*
             * Valid ADS1292R status begins with binary 1100.
             */
            if ((ecg_raw[0] & 0xF0U) != 0xC0U)
            {
                continue;
            }

            ch2_raw = ADS1292R_Convert24Bit(
                ecg_raw[6],
                ecg_raw[7],
                ecg_raw[8]);

            (void)ADS1292R_ProcessCH2Sample(
                &ch2_filter,
                ch2_raw,
                &ch2_output);

            ch2_bandpass_int = (int32_t)ch2_output.bandpass;
            ch2_notch_int = (int32_t)ch2_output.notch;
            ch2_bandpass_notch_int = (int32_t)ch2_output.bandpass_notch;
            ch2_all_filter_int = (int32_t)ch2_output.all_filter;

            timestamp_ms = HAL_GetTick() - start_tick;

            length = snprintf(
                (char *)usb_buffer,
                sizeof(usb_buffer),
                "%lu,%lu,%02X%02X%02X%02X%02X%02X%02X%02X%02X,"
                "%ld,%ld,%ld,%ld,%ld\r\n",
                (unsigned long)timestamp_ms,
                (unsigned long)sample_counter,
                (unsigned int)ecg_raw[0],
                (unsigned int)ecg_raw[1],
                (unsigned int)ecg_raw[2],
                (unsigned int)ecg_raw[3],
                (unsigned int)ecg_raw[4],
                (unsigned int)ecg_raw[5],
                (unsigned int)ecg_raw[6],
                (unsigned int)ecg_raw[7],
                (unsigned int)ecg_raw[8],
                (long)ch2_raw,
                (long)ch2_bandpass_int,
                (long)ch2_notch_int,
                (long)ch2_bandpass_notch_int,
                (long)ch2_all_filter_int);

            /*
             * Check that snprintf succeeded and did not overflow.
             */
            if ((length <= 0) ||
                (length >= (int)sizeof(usb_buffer)))
            {
                continue;
            }

            actual_length = 0;

            status = ux_device_class_cdc_acm_write(
                g_cdc_acm,
                usb_buffer,
                (ULONG)length,
                &actual_length);

            if (status != UX_SUCCESS)
            {
                tx_thread_sleep(1);
            }
        }
        else
        {
            /*
             * Update the DRDY state.
             * This rearms detection after DRDY returns HIGH.
             */
            previous_drdy = current_drdy;

            /*
             * Allow other ThreadX threads to execute.
             */
            tx_thread_relinquish();
        }
    }
}


/* USER CODE END 1 */
