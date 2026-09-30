/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : custom_bus.h
  * @brief          : header file for the BSP BUS IO driver
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
#ifndef CUSTOM_BUS_H
#define CUSTOM_BUS_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "custom_conf.h"
#include "custom_errno.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup CUSTOM
  * @{
  */

/** @defgroup CUSTOM_BUS CUSTOM BUS
  * @{
  */

/** @defgroup CUSTOM_BUS_Exported_Constants CUSTOM BUS Exported Constants
  * @{
  */

#define BUS_USART2_INSTANCE USART2
#define BUS_USART2_TX_GPIO_AF GPIO_AF7_USART2
#define BUS_USART2_TX_GPIO_PIN GPIO_PIN_2
#define BUS_USART2_TX_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define BUS_USART2_TX_GPIO_PORT GPIOA
#define BUS_USART2_TX_GPIO_CLK_DISABLE() __HAL_RCC_GPIOA_CLK_DISABLE()
#define BUS_USART2_RX_GPIO_PORT GPIOA
#define BUS_USART2_RX_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define BUS_USART2_RX_GPIO_CLK_DISABLE() __HAL_RCC_GPIOA_CLK_DISABLE()
#define BUS_USART2_RX_GPIO_PIN GPIO_PIN_3
#define BUS_USART2_RX_GPIO_AF GPIO_AF7_USART2

#ifndef BUS_USART2_BAUDRATE
   #define BUS_USART2_BAUDRATE  9600U /* baud rate of UARTn = 9600 baud*/
#endif
#ifndef BUS_USART2_POLL_TIMEOUT
   #define BUS_USART2_POLL_TIMEOUT                9600U
#endif

/**
  * @}
  */

/** @defgroup CUSTOM_BUS_Private_Types CUSTOM BUS Private types
  * @{
  */
#if (USE_HAL_UART_REGISTER_CALLBACKS  == 1U)
typedef struct
{
  pUART_CallbackTypeDef  pMspInitCb;
  pUART_CallbackTypeDef  pMspDeInitCb;
}BSP_UART_Cb_t;
#endif /* (USE_HAL_UART_REGISTER_CALLBACKS == 1U) */

/**
  * @}
  */

/** @defgroup CUSTOM_LOW_LEVEL_Exported_Variables LOW LEVEL Exported Constants
  * @{
  */

extern UART_HandleTypeDef huart2;

/**
  * @}
  */

/** @addtogroup CUSTOM_BUS_Exported_Functions
  * @{
  */

HAL_StatusTypeDef MX_USART2_UART_Init(UART_HandleTypeDef* huart);
int32_t BSP_USART2_Init(void);
int32_t BSP_USART2_DeInit(void);
int32_t BSP_USART2_Send(uint8_t *pData, uint16_t Length);
int32_t BSP_USART2_Recv(uint8_t *pData, uint16_t Length);
#if (USE_HAL_UART_REGISTER_CALLBACKS == 1U)
int32_t BSP_USART2_RegisterDefaultMspCallbacks (void);
int32_t BSP_USART2_RegisterMspCallbacks (BSP_UART_Cb_t *Callbacks);
#endif /* (USE_HAL_UART_REGISTER_CALLBACKS == 1U)  */
int32_t BSP_USART2_Recv_DMA(uint8_t *pData, uint16_t Length);

int32_t BSP_GetTick(void);

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif /* CUSTOM_BUS_H */

