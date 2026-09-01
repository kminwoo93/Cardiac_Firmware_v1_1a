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
	UCHAR usb_buffer[64];

	    uint8_t ecg_raw[9];
	    int32_t ch1;
	    int32_t ch2;

	    ULONG actual_length;
	    UINT status;
	    int length;

	    TX_PARAMETER_NOT_USED(thread_input);

	    while (1)
	    {
	        if ((g_cdc_acm != UX_NULL) &&
	            (_ux_system_slave->ux_system_slave_device.ux_slave_device_state ==
	             UX_DEVICE_CONFIGURED))
	        {
	            if (HAL_GPIO_ReadPin(DRDY_GPIO_Port, DRDY_Pin) == GPIO_PIN_RESET)
	            {
	                ADS1292R_ReadData(ecg_raw);

	                ch1 = ADS1292R_Convert24Bit(
	                    ecg_raw[3],
	                    ecg_raw[4],
	                    ecg_raw[5]);

	                ch2 = ADS1292R_Convert24Bit(
	                    ecg_raw[6],
	                    ecg_raw[7],
	                    ecg_raw[8]);

	                length = snprintf((char *)usb_buffer,
	                                  sizeof(usb_buffer),
	                                  "%ld,%ld\r\n",
	                                  (long)ch1,
	                                  (long)ch2);

	                actual_length = 0;

	                status = ux_device_class_cdc_acm_write(
	                    g_cdc_acm,
	                    usb_buffer,
	                    length,
	                    &actual_length);

	                if (status != UX_SUCCESS)
	                {
	                    tx_thread_sleep(1);
	                }
	            }
	        }
	        else
	        {
	            tx_thread_sleep(10);
	        }
	    }
}


/* USER CODE END 1 */
