/**
  ******************************************************************************
  * @file    hci_tl_interface.h
  * @author  SRA Application Team
  * @brief   Header file for hci_tl_interface.c
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef HCI_TL_INTERFACE_H
#define HCI_TL_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "custom_bus.h"

/* Exported Defines ----------------------------------------------------------*/

#define HCI_TL_SPI_EXTI_PORT
#define HCI_TL_SPI_EXTI_PIN
#define HCI_TL_SPI_EXTI_IRQ_N

#define HCI_TL_SPI_IRQ_PORT
#define HCI_TL_SPI_IRQ_PIN

#define HCI_TL_SPI_CS_PORT
#define HCI_TL_SPI_CS_PIN

#define HCI_TL_RST_PORT       GPIOA
#define HCI_TL_RST_PIN        GPIO_PIN_1

/* Exported variables --------------------------------------------------------*/
extern EXTI_HandleTypeDef     hexti;
#define H_EXTI_ hexti
#define UART_INSTANCE huart2
/* Exported Functions --------------------------------------------------------*/
int32_t HCI_TL_SPI_Init(void *pConf);
int32_t HCI_TL_SPI_DeInit(void);
int32_t HCI_TL_SPI_Receive(uint8_t *buffer, uint16_t size);
int32_t HCI_TL_SPI_Send(uint8_t *buffer, uint16_t size);
int32_t HCI_TL_SPI_Reset(void);

int32_t HCI_TL_UART_Init(void *pConf);
int32_t HCI_TL_UART_DeInit(void);
int32_t HCI_TL_UART_Receive(uint8_t *buffer, uint16_t size);
int32_t HCI_TL_UART_Send(uint8_t *buffer, uint16_t size);
int32_t HCI_TL_UART_Reset(void);

/**
  * @brief  Register hci_tl_interface IO bus services
  *
  * @param  None
  * @retval None
  */
void hci_tl_lowlevel_init(void);
#ifdef __cplusplus
}
#endif
#endif /* HCI_TL_INTERFACE_H */
