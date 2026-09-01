/*
 * ads1292r.c
 *
 *  Created on: Aug 31, 2026
 *      Author: Minwoo Kim
 */


#include "ads1292r.h"
#include "main.h"

extern SPI_HandleTypeDef hspi1;

static void ADS1292R_Select(void)
{
	HAL_GPIO_WritePin(CS_ADS_GPIO_Port,
						CS_ADS_Pin,
						GPIO_PIN_RESET);
}

static void ADS1292R_Deselect(void)
{
	HAL_GPIO_WritePin(CS_ADS_GPIO_Port,
						CS_ADS_Pin,
						GPIO_PIN_SET);
}

void ADS1292R_SendCommand(uint8_t command)
{
    ADS1292R_Select();

    HAL_SPI_Transmit(&hspi1,
                     &command,
                     1,
                     HAL_MAX_DELAY);
    HAL_Delay(1);
    ADS1292R_Deselect();
}

uint8_t ADS1292R_ReadRegister(uint8_t address)
{
    uint8_t command;
    uint8_t number_of_registers = 0x00;
    uint8_t value = 0;

    command = ADS1292R_CMD_RREG | address;

    ADS1292R_Select();

    HAL_SPI_Transmit(&hspi1,
                     &command,
                     1,
                     HAL_MAX_DELAY);

    HAL_SPI_Transmit(&hspi1,
                     &number_of_registers,
                     1,
                     HAL_MAX_DELAY);

    HAL_SPI_Receive(&hspi1,
                    &value,
                    1,
                    HAL_MAX_DELAY);
    HAL_Delay(1);
    ADS1292R_Deselect();

    return value;
}

void ADS1292R_WriteRegister(uint8_t address, uint8_t value)
{
    uint8_t command;
    uint8_t number_of_registers = 0x00;

    command = ADS1292R_CMD_WREG | address;

    ADS1292R_Select();

    HAL_SPI_Transmit(&hspi1,
                     &command,
                     1,
                     HAL_MAX_DELAY);

    HAL_SPI_Transmit(&hspi1,
                     &number_of_registers,
                     1,
                     HAL_MAX_DELAY);

    HAL_SPI_Transmit(&hspi1,
                     &value,
                     1,
                     HAL_MAX_DELAY);
    HAL_Delay(1);
    ADS1292R_Deselect();
}

void ADS1292R_HardwareReset(void)
{
	// Make sure ADS1292R is active first
	    HAL_GPIO_WritePin(PWDN_GPIO_Port,
	                      PWDN_Pin,
	                      GPIO_PIN_SET);

	    HAL_Delay(1000);

	    // Reset pulse
	       HAL_GPIO_WritePin(PWDN_GPIO_Port,
	                         PWDN_Pin,
	                         GPIO_PIN_RESET);

	       HAL_Delay(1);

	       HAL_GPIO_WritePin(PWDN_GPIO_Port,
	                         PWDN_Pin,
	                         GPIO_PIN_SET);

	       HAL_Delay(10);
}

void ADS1292R_ReadData(uint8_t *data)
{
    ADS1292R_Select();

    HAL_SPI_Receive(&hspi1,
                    data,
                    9,
                    HAL_MAX_DELAY);

    ADS1292R_Deselect();
}

int32_t ADS1292R_Convert24Bit(uint8_t b0,
                              uint8_t b1,
                              uint8_t b2)
{
    int32_t value;

    value = ((int32_t)b0 << 16) |
            ((int32_t)b1 << 8)  |
            ((int32_t)b2);

    /* 24-bit two's complement → 32-bit signed integer */
    if (value & 0x00800000)
    {
        value |= 0xFF000000;
    }

    return value;
}
