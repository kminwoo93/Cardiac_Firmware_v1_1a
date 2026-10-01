/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : custom_bus.c
  * @brief          : source file for the BSP BUS IO driver
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
#include "custom_bus.h"

__weak HAL_StatusTypeDef MX_USART2_UART_Init(UART_HandleTypeDef* huart);

/** @addtogroup BSP
  * @{
  */

/** @addtogroup CUSTOM
  * @{
  */

/** @defgroup CUSTOM_BUS CUSTOM BUS
  * @{
  */

/** @defgroup CUSTOM_BUS_Exported_Variables BUS Exported Variables
  * @{
  */

UART_HandleTypeDef huart2;
/**
  * @}
  */

/** @defgroup CUSTOM_BUS_Private_Variables BUS Private Variables
  * @{
  */

#if (USE_HAL_UART_REGISTER_CALLBACKS == 1U)
static uint32_t IsUSART2MspCbValid = 0;
#endif /* USE_HAL_UART_REGISTER_CALLBACKS */
static uint32_t USART2InitCounter = 0;

/**
  * @}
  */

/** @defgroup CUSTOM_BUS_Private_FunctionPrototypes  BUS Private Function
  * @{
  */

static void USART2_MspInit(UART_HandleTypeDef* huart);
static void USART2_MspDeInit(UART_HandleTypeDef* huart);

/**
  * @}
  */

/** @defgroup CUSTOM_LOW_LEVEL_Private_Functions CUSTOM LOW LEVEL Private Functions
  * @{
  */

/** @defgroup CUSTOM_BUS_Exported_Functions CUSTOM_BUS Exported Functions
  * @{
  */

/* BUS IO driver over USART Peripheral */
/*******************************************************************************
                            BUS OPERATIONS OVER USART
*******************************************************************************/
/**
  * @brief  Initializes USART HAL.
  * @param  Init : UART initialization parameters
  * @retval BSP status
  */
int32_t BSP_USART2_Init(void)
{
  int32_t ret = BSP_ERROR_NONE;

  huart2.Instance  = USART2;

  if(USART2InitCounter++ == 0)
  {
    if (HAL_UART_GetState(&huart2) == HAL_UART_STATE_RESET)
    {
#if (USE_HAL_UART_REGISTER_CALLBACKS == 0U)
      /* Init the UART Msp */
      USART2_MspInit(&huart2);
#else
      if(IsUSART2MspCbValid == 0U)
      {
        if(BSP_USART2_RegisterDefaultMspCallbacks() != BSP_ERROR_NONE)
        {
          return BSP_ERROR_MSP_FAILURE;
        }
      }
#endif
      if(ret == BSP_ERROR_NONE)
      {
        /* Init the UART */
        if (MX_USART2_UART_Init(&huart2) != HAL_OK)
        {
          ret = BSP_ERROR_BUS_FAILURE;
        }
      }
    }
  }
  return ret;
}

/**
  * @brief  DeInitializes UART HAL.
  * @retval None
  * @retval BSP status
  */
int32_t BSP_USART2_DeInit(void)
{
  int32_t ret = BSP_ERROR_BUS_FAILURE;

  if (USART2InitCounter > 0)
  {
    if (--USART2InitCounter == 0)
    {
#if (USE_HAL_UART_REGISTER_CALLBACKS == 0U)
      USART2_MspDeInit(&huart2);
#endif
      /* DeInit the UART*/
      if (HAL_UART_DeInit(&huart2) == HAL_OK)
      {
        ret = BSP_ERROR_NONE;
      }
    }
  }
  return ret;
}

/**
  * @brief  Write Data through UART BUS.
  * @param  pData: Pointer to data buffer to send
  * @param  Length: Length of data in byte
  * @retval BSP status
  */
int32_t BSP_USART2_Send(uint8_t *pData, uint16_t Length)
{
  int32_t ret = BSP_ERROR_UNKNOWN_FAILURE;

  if(HAL_UART_Transmit(&huart2, pData, Length, BUS_USART2_POLL_TIMEOUT) == HAL_OK)
  {
      ret = BSP_ERROR_NONE;
  }
  return ret;
}

/**
  * @brief  Receive Data from UART BUS
  * @param  pData: Pointer to data buffer to receive
  * @param  Length: Length of data in byte
  * @retval BSP status
  */
int32_t  BSP_USART2_Recv(uint8_t *pData, uint16_t Length)
{
  int32_t ret = BSP_ERROR_UNKNOWN_FAILURE;

  if(HAL_UART_Receive(&huart2, pData, Length, BUS_USART2_POLL_TIMEOUT) == HAL_OK)
  {
      ret = BSP_ERROR_NONE;
  }
  return ret;
}

/**
  * @brief  Receive Data from USART BUS with DMA
  * @param  pData: Pointer to data buffer to receive
  * @param  Length: Length of data in byte
  * @retval BSP status
  */
int32_t  BSP_USART2_Recv_DMA(uint8_t *pData, uint16_t Length)
{
  int32_t ret = BSP_ERROR_NONE;

  if(HAL_UART_Receive_DMA(&huart2, pData, Length) != HAL_OK)
  {
      ret = BSP_ERROR_UNKNOWN_FAILURE;
  }
  return ret;
}

#if (USE_HAL_UART_REGISTER_CALLBACKS == 1U)
/**
  * @brief Register Default BSP USART2 Bus Msp Callbacks
  * @retval BSP status
  */
int32_t BSP_USART2_RegisterDefaultMspCallbacks (void)
{

  __HAL_UART_RESET_HANDLE_STATE(&huart2);

  /* Register MspInit Callback */
  if (HAL_UART_RegisterCallback(&huart2, HAL_UART_MSPINIT_CB_ID, USART2_MspInit)  != HAL_OK)
  {
    return BSP_ERROR_PERIPH_FAILURE;
  }

  /* Register MspDeInit Callback */
  if (HAL_UART_RegisterCallback(&huart2, HAL_UART_MSPDEINIT_CB_ID, USART2_MspDeInit) != HAL_OK)
  {
    return BSP_ERROR_PERIPH_FAILURE;
  }
  IsUSART2MspCbValid = 1;

  return BSP_ERROR_NONE;
}

/**
  * @brief BSP USART2 Bus Msp Callback registering
  * @param Callbacks     pointer to USART2 MspInit/MspDeInit callback functions
  * @retval BSP status
  */
int32_t BSP_USART2_RegisterMspCallbacks (BSP_UART_Cb_t *Callbacks)
{
  /* Prevent unused argument(s) compilation warning */
  __HAL_UART_RESET_HANDLE_STATE(&huart2);

   /* Register MspInit Callback */
  if (HAL_UART_RegisterCallback(&huart2, HAL_UART_MSPINIT_CB_ID, Callbacks->pMspInitCb)  != HAL_OK)
  {
    return BSP_ERROR_PERIPH_FAILURE;
  }

  /* Register MspDeInit Callback */
  if (HAL_UART_RegisterCallback(&huart2, HAL_UART_MSPDEINIT_CB_ID, Callbacks->pMspDeInitCb) != HAL_OK)
  {
    return BSP_ERROR_PERIPH_FAILURE;
  }

  IsUSART2MspCbValid = 1;

  return BSP_ERROR_NONE;
}
#endif /* USE_HAL_UART_REGISTER_CALLBACKS */

/**
  * @brief  Return system tick in ms
  * @retval Current HAL time base time stamp
  */
int32_t BSP_GetTick(void) {
  return HAL_GetTick();
}

/* USART2 init function */

__weak HAL_StatusTypeDef MX_USART2_UART_Init(UART_HandleTypeDef* huart)
{
  HAL_StatusTypeDef ret = HAL_OK;

  huart->Instance = USART2;
  huart->Init.BaudRate = 921600;
  huart->Init.WordLength = UART_WORDLENGTH_8B;
  huart->Init.StopBits = UART_STOPBITS_1;
  huart->Init.Parity = UART_PARITY_NONE;
  huart->Init.Mode = UART_MODE_TX_RX;
  huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart->Init.OverSampling = UART_OVERSAMPLING_16;
  huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(huart) != HAL_OK)
  {
    ret = HAL_ERROR;
  }

  if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    ret = HAL_ERROR;
  }

  if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    ret = HAL_ERROR;
  }

  if (HAL_UARTEx_DisableFifoMode(huart) != HAL_OK)
  {
    ret = HAL_ERROR;
  }

  return ret;
}
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

static void USART2_MspInit(UART_HandleTypeDef* uartHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct;
  DMA_NodeConfTypeDef NodeConfig;
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
    PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    /* Enable Peripheral clock */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = BUS_USART2_TX_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = BUS_USART2_TX_GPIO_AF;
    HAL_GPIO_Init(BUS_USART2_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BUS_USART2_RX_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    /* Keep RX idle-high while the STM32WB05N TX is not driven (reset/boot) */
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = BUS_USART2_RX_GPIO_AF;
    HAL_GPIO_Init(BUS_USART2_RX_GPIO_PORT, &GPIO_InitStruct);

    /* Peripheral DMA init*/

    NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
    NodeConfig.Init.Request = GPDMA1_REQUEST_USART2_RX;
    NodeConfig.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    NodeConfig.Init.Direction = DMA_PERIPH_TO_MEMORY;
    NodeConfig.Init.SrcInc = DMA_SINC_FIXED;
    NodeConfig.Init.DestInc = DMA_DINC_INCREMENTED;
    NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    NodeConfig.Init.SrcBurstLength = 1;
    NodeConfig.Init.DestBurstLength = 1;
    NodeConfig.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
    NodeConfig.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    NodeConfig.Init.Mode = DMA_NORMAL;
    NodeConfig.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
    NodeConfig.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel0);

    HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel0, NULL, &Node_GPDMA1_Channel0);

    HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel0);

    handle_GPDMA1_Channel0.Instance = GPDMA1_Channel0;
    handle_GPDMA1_Channel0.InitLinkedList.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    handle_GPDMA1_Channel0.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
    handle_GPDMA1_Channel0.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
    handle_GPDMA1_Channel0.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_GPDMA1_Channel0.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
    HAL_DMAEx_List_Init(&handle_GPDMA1_Channel0);

    HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel0, &List_GPDMA1_Channel0);

    __HAL_LINKDMA(uartHandle, hdmarx, handle_GPDMA1_Channel0);

    HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel0, DMA_CHANNEL_NPRIV);

    /* Peripheral interrupt init */
    HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspInit 1 */

  /* USER CODE END USART2_MspInit 1 */
}

static void USART2_MspDeInit(UART_HandleTypeDef* uartHandle)
{
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    HAL_GPIO_DeInit(BUS_USART2_TX_GPIO_PORT, BUS_USART2_TX_GPIO_PIN);

    HAL_GPIO_DeInit(BUS_USART2_RX_GPIO_PORT, BUS_USART2_RX_GPIO_PIN);

    /* Peripheral DMA DeInit*/
    HAL_DMA_DeInit(uartHandle->hdmarx);

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(USART2_IRQn);

  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
}

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

