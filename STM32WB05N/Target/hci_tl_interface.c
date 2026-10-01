/**
  ******************************************************************************
  * @file    hci_tl_interface.c
  * @author  SRA Application Team
  * @brief   This file provides the implementation for all functions prototypes
  *          for the Host - STM32WB05N HCI Transport Layer interface.
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

/* Includes ------------------------------------------------------------------*/
#include "RTE_Components.h"

#include "hci_tl.h"
#include "hci_parser.h"
#include "hci_const.h"
#include "cardiac_ble.h"

/* Defines -------------------------------------------------------------------*/

#define HEADER_SIZE       5U
#define MAX_BUFFER_SIZE   255U
#define TIMEOUT_DURATION  100U
#define TIMEOUT_IRQ_HIGH  1000U

/* UART timeout values */
#define BLE_UART_SHORT_TIMEOUT      30
#define BLE_UART_LONG_TIMEOUT       300
#define UART_ARRAY_SIZE             532U

/* Private variables ---------------------------------------------------------*/
EXTI_HandleTypeDef hexti;
uint8_t  RxBuffer[UART_ARRAY_SIZE];
__IO uint32_t     uwNbReceivedChars;
uint8_t *pBufferReadyForUser;
uint8_t *pBufferReadyForReception;
/* brief Data buffers used to manage received data in interrupt routine */
uint8_t aRXBufferA[UART_ARRAY_SIZE];
uint8_t aRXBufferB[UART_ARRAY_SIZE];
/* Position in RxBuffer up to which received data have been processed */
static uint16_t old_pos = 0;
/* Number of times reception was restarted after a UART error */
volatile uint32_t hci_tl_uart_rx_restart_count = 0;
/* Private function prototypes -----------------------------------------------*/
static void HCI_TL_UART_RestartRx(void);
/******************** IO Operation and BUS services ***************************/

/**
  * @brief  Initializes the peripherals communication with the STM32WB05N
  *         Expansion Board (via SPI, I2C, USART, ...)
  *
  * @param  void* Pointer to configuration struct
  * @retval int32_t Status
  */
int32_t HCI_TL_UART_Init(void *pConf)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* Configure RESET Line.
     Open-drain: the STM32WB05N drives NRST low itself on internal resets
     (HCI_Reset triggers NVIC_SystemReset), so the line must not be driven
     high. The WB05N internal pull-up provides the high level. */
  HAL_GPIO_WritePin(HCI_TL_RST_PORT, HCI_TL_RST_PIN, GPIO_PIN_SET);
  GPIO_InitStruct.Pin =  HCI_TL_RST_PIN ;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HCI_TL_RST_PORT, &GPIO_InitStruct);

  BSP_USART2_Init();
  /* Initializes Buffer swap mechanism (used in User callback) :
  - 2 physical buffers aRXBufferA and aRXBufferB (RX_BUFFER_SIZE length)
  */
  pBufferReadyForReception = aRXBufferA;
  pBufferReadyForUser      = aRXBufferB;

  uwNbReceivedChars        = 0;
  old_pos                  = 0;

  return HAL_UARTEx_ReceiveToIdle_DMA(&UART_INSTANCE, RxBuffer, UART_ARRAY_SIZE);
}

/**
  * @brief  Restart reception after it was aborted.
  *         Any UART error (FE/NE/ORE) during DMA reception makes the HAL abort
  *         the reception, and nothing would restart it otherwise.
  * @param  None
  * @retval None
  */
static void HCI_TL_UART_RestartRx(void)
{
  (void)HAL_UART_AbortReceive(&UART_INSTANCE);
  __HAL_UART_CLEAR_FLAG(&UART_INSTANCE,
                        UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
  old_pos = 0;
  hci_tl_uart_rx_restart_count++;
  (void)HAL_UARTEx_ReceiveToIdle_DMA(&UART_INSTANCE, RxBuffer, UART_ARRAY_SIZE);
}

/**
  * @brief  DeInitializes the peripherals communication with the STM32WB05N
  *         Expansion Board (via SPI, I2C, USART, ...)
  *
  * @param  None
  * @retval int32_t 0
  */
int32_t HCI_TL_UART_DeInit(void)
{
  HAL_GPIO_DeInit(HCI_TL_RST_PORT, HCI_TL_RST_PIN);
  return 0;
}

/**
  * @brief Reset STM32WB05N module.
  *
  * @param  None
  * @retval int32_t 0
  */
int32_t HCI_TL_UART_Reset(void)
{
  HAL_GPIO_WritePin(HCI_TL_RST_PORT, HCI_TL_RST_PIN, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(HCI_TL_RST_PORT, HCI_TL_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(150);
  return 0;
}

/**
  * @brief  Reads from STM32WB05N UART buffer and store data into local buffer.
  *
  * @param  buffer : Buffer where data from UART are stored
  * @param  size   : Buffer size
  * @retval int32_t: Number of read bytes
  */
int32_t HCI_TL_UART_Receive(uint8_t *buffer, uint16_t size)
{
  return BSP_USART2_Recv(buffer, size);
}

/**
  * @brief  Writes data from local buffer to UART.
  *
  * @param  buffer : data buffer to be written
  * @param  size   : size of first data buffer to be written
  * @retval int32_t: Number of read bytes
  */
int32_t HCI_TL_UART_Send(uint8_t *buffer, uint16_t size)
{
  return BSP_USART2_Send(buffer, size);
}
/***************************** hci_tl_interface main functions *****************************/
/**
  * @brief  Register hci_tl_interface IO bus services
  *
  * @param  None
  * @retval None
  */
void hci_tl_lowlevel_init(void)
{
  /* USER CODE BEGIN hci_tl_lowlevel_init 1 */

  /* USER CODE END hci_tl_lowlevel_init 1 */
  tHciIO fops;

  /* Register IO bus services */
  fops.Init    = HCI_TL_UART_Init;
  fops.DeInit  = HCI_TL_UART_DeInit;
  fops.Send    = HCI_TL_UART_Send;
  fops.Receive = HCI_TL_UART_Receive;
  fops.Reset   = HCI_TL_UART_Reset;
  fops.GetTick = BSP_GetTick;

  hci_register_io_bus(&fops);

  /* USER CODE BEGIN hci_tl_lowlevel_init 2 */

  /* USER CODE END hci_tl_lowlevel_init 2 */

  /* USER CODE BEGIN hci_tl_lowlevel_init 3 */

  /* USER CODE END hci_tl_lowlevel_init 3 */

}
/**
  * @brief  UART interrupt callback.
  * @param  huart handle
  * @param  buffer size
  * @retval None
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  uint8_t *ptemp;
  uint16_t i;
  if (huart == &UART_INSTANCE)
  {
  /* In circular mode Size == buffer size means "position wrapped to 0".
     An IDLE event with no new data also reports the full buffer size. */
  if (Size == UART_ARRAY_SIZE)
  {
    Size = 0;
  }
  /* Check if number of received data in recpetion buffer has changed */
  if (Size != old_pos)
  {
    /* Check if position of index in reception buffer has simply be increased
       of if end of buffer has been reached */
    if (Size > old_pos)
    {
      /* Current position is higher than previous one */
      uwNbReceivedChars = Size - old_pos;
      /* Copy received data in "User" buffer for evacuation */
      for (i = 0; i < uwNbReceivedChars; i++)
      {
        pBufferReadyForUser[i] = RxBuffer[old_pos + i];
      }
    }
    else
    {
      /* Current position is lower than previous one : end of buffer has been reached */
      /* First copy data from current position till end of buffer */
      uwNbReceivedChars = UART_ARRAY_SIZE - old_pos;
      /* Copy received data in "User" buffer for evacuation */
      for (i = 0; i < uwNbReceivedChars; i++)
      {
        pBufferReadyForUser[i] = RxBuffer[old_pos + i];
      }
      /* Check and continue with beginning of buffer */
      if (Size > 0)
      {
        for (i = 0; i < Size; i++)
        {
          pBufferReadyForUser[uwNbReceivedChars + i] = RxBuffer[i];
        }
        uwNbReceivedChars += Size;
      }
    }
    /* Process received data that has been extracted from Rx User buffer */
    hci_input_event(pBufferReadyForUser, uwNbReceivedChars);

    /* Wake up the BLE thread to process the queued events */
    Cardiac_BLE_RxNotify();

    /* Swap buffers for next bytes to be processed */
    ptemp = pBufferReadyForUser;
    pBufferReadyForUser = pBufferReadyForReception;
    pBufferReadyForReception = ptemp;
  }
  /* Update old_pos as new reference of position in User Rx buffer that
     indicates position to which data have been processed */
  old_pos = Size;

  }
}

/**
  * @brief  UART error callback.
  * @param  huart handle
  * @retval None
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == &UART_INSTANCE)
  {
    HCI_TL_UART_RestartRx();
  }
}
