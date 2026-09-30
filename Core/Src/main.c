/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "gpdma.h"
#include "i2c.h"
#include "icache.h"
#include "spi.h"
#include "tim.h"
#include "usb_otg.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ads1292r.h"
#include "icm20948.h"
#include "cardiac_ble.h"
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
/*
 * Upper 32 bits of the common 1 MHz timestamp.
 *
 * TIM2->CNT is the lower 32 bits and wraps every
 * 4294967296 us, approximately 71.58 minutes.
 */
volatile uint32_t timestamp_overflow_count = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void SystemPower_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Capture the common 1 MHz timestamp.
 *
 * TIM2->CNT provides the lower 32 bits.
 * timestamp_overflow_count provides the upper 32 bits.
 *
 * The values are read repeatedly if an overflow occurred
 * during the capture.
 */
void Timestamp_CaptureFromISR(
    uint32_t *timestamp_high,
    uint32_t *timestamp_low)
{
    uint32_t high_before;
    uint32_t high_after;
    uint32_t low;

    do
    {
        high_before = timestamp_overflow_count;

        low = __HAL_TIM_GET_COUNTER(&htim2);

        high_after = timestamp_overflow_count;
    }
    while (high_before != high_after);

    *timestamp_high = high_before;
    *timestamp_low = low;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the System Power */
  SystemPower_Config();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_ICACHE_Init();
  MX_I2C1_Init();
  MX_USB_OTG_HS_PCD_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  Cardiac_BLE_Init();
  /*
   * Reset and start the common 1 MHz timestamp timer
   * before either sensor starts producing interrupts.
   */
  timestamp_overflow_count = 0U;
  __HAL_TIM_SET_COUNTER(&htim2, 0U);

  if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
  {
      Error_Handler();
  }
  /*
   * Existing sensor initialization starts below.
   */

  volatile HAL_StatusTypeDef icm_init_status;

  icm_init_status = ICM20948_Init();

  if (icm_init_status != HAL_OK)
  {
	    icm_init_status =
	        ICM20948_EnableDataReadyInterrupt();
  }

//

  ADS1292R_HardwareReset();
  HAL_Delay(10);
  ADS1292R_SendCommand(ADS1292R_CMD_SDATAC);
  HAL_Delay(10);

  /* 500 samples/second */
  ADS1292R_WriteRegister(ADS1292R_REG_CONFIG1, 0x02);
  HAL_Delay(10);
  /* Internal reference + internal test signal */
  ADS1292R_WriteRegister(ADS1292R_REG_CONFIG2, 0xA0);
  /* Allow internal reference to stabilize */
  HAL_Delay(150);
  /* CH1: no use */
  ADS1292R_WriteRegister(ADS1292R_REG_CH1SET, 0x81);
  HAL_Delay(10);
  /* CH2: gain 6, normal electrode input */
  ADS1292R_WriteRegister(ADS1292R_REG_CH2SET, 0x00);
  HAL_Delay(10);
  ADS1292R_WriteRegister(ADS1292R_REG_RESP1,0x02);
  HAL_Delay(10);
  ADS1292R_WriteRegister(ADS1292R_REG_RESP2,0x03);
  HAL_Delay(10);
  ADS1292R_WriteRegister(ADS1292R_REG_LOFF_SENS, 0x00);
  HAL_Delay(10);
  /* RLD buffer ON, derived from CH1P and CH1N */
  ADS1292R_WriteRegister(ADS1292R_REG_RLD_SENS, 0x2C);
  HAL_Delay(10);


  /* Verify */
  volatile uint8_t config1_check = 0;
  volatile uint8_t config2_check = 0;
  volatile uint8_t ch1set_check  = 0;
  volatile uint8_t ch2set_check  = 0;
  volatile uint8_t resp2_readback;
  volatile uint8_t rld_readback;

  resp2_readback =
      ADS1292R_ReadRegister(ADS1292R_REG_RESP2);
  HAL_Delay(10);
  rld_readback =
      ADS1292R_ReadRegister(ADS1292R_REG_RLD_SENS);
  HAL_Delay(10);
  config1_check = ADS1292R_ReadRegister(ADS1292R_REG_CONFIG1);
  config2_check = ADS1292R_ReadRegister(ADS1292R_REG_CONFIG2);
  ch1set_check  = ADS1292R_ReadRegister(ADS1292R_REG_CH1SET);
  ch2set_check  = ADS1292R_ReadRegister(ADS1292R_REG_CH2SET);

  /* Start conversion */
   ADS1292R_SendCommand(ADS1292R_CMD_START);
  HAL_Delay(10);
  /* Continuous read mode */
  ADS1292R_SendCommand(ADS1292R_CMD_RDATAC);
  HAL_Delay(10);

  /* USER CODE END 2 */

  MX_ThreadX_Init();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_4;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Power Configuration
  * @retval None
  */
static void SystemPower_Config(void)
{

  /*
   * Switch to SMPS regulator instead of LDO
   */
  if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
/* USER CODE BEGIN PWR */
/* USER CODE END PWR */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  /*
   * TIM2 is the common 1 MHz free-running timestamp timer.
   * Increment the upper 32-bit word whenever TIM2 wraps.
   */
  if (htim->Instance == TIM2)
  {
      timestamp_overflow_count++;
  }
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
