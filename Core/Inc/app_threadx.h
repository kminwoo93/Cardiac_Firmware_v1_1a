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

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

typedef struct
{
  uint64_t timestamp_us;
  uint32_t sample_counter;
  uint32_t drdy_counter;
  uint32_t status;
  int32_t ch1_raw;
  int32_t ch2_raw;
  float ch2_filtered;
  uint32_t error_flags;
} ADS1292R_Record;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Main thread defines -------------------------------------------------------*/
/* USER CODE BEGIN MTD */

/* USER CODE END MTD */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
UINT App_ThreadX_Init(VOID *memory_ptr);
void MX_ThreadX_Init(void);

/* USER CODE BEGIN EFP */

void ADS1292R_DRDY_EXTI_Callback(void);
void ADS1292R_USB_Activated(void);
void ADS1292R_USB_Deactivated(void);
UINT ADS1292R_WaitAndPopBatch(ADS1292R_Record records[10]);
uint32_t ADS1292R_ProfileNow(void);

extern volatile uint32_t ads_drdy_count;
extern volatile uint32_t ads_sample_count;
extern volatile uint32_t ads_overrun_count;
extern volatile uint32_t ads_bad_status_count;
extern volatile uint32_t ads_spi_error_count;
extern volatile uint32_t ads_buffer_overflow_count;
extern volatile uint32_t ads_usb_error_count;
extern volatile uint32_t ads_usb_reconnect_count;

/* Diagnostic counter rules:
 * drdy: every DRDY EXTI; sample: every valid record published; overrun: edge
 * rejected/replaced while busy/pending; bad_status: invalid 0xCxxxxx prefix;
 * spi_error: non-HAL_OK 9-byte read; buffer_overflow: oldest record discarded;
 * usb_error: format/write/short-write failure; usb_reconnect: CDC activation. */

extern volatile uint32_t ads_latency_us_last;
extern volatile uint32_t ads_latency_us_max;
extern volatile uint32_t ads_cs_duration_us_last;
extern volatile uint32_t ads_cs_duration_us_max;
extern volatile uint32_t ads_acquisition_us_last;
extern volatile uint32_t ads_acquisition_us_max;
extern volatile uint32_t ads_usb_format_us_last;
extern volatile uint32_t ads_usb_format_us_max;
extern volatile uint32_t ads_usb_write_us_last;
extern volatile uint32_t ads_usb_write_us_max;

/* USER CODE END EFP */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_THREADX_H */
