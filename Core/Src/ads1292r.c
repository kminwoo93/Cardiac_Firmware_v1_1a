/*
 * ads1292r.c
 *
 *  Created on: Aug 31, 2026
 *      Author: Minwoo Kim
 */


#include "ads1292r.h"
#include "main.h"
#include <string.h>

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

static float ADS1292R_ApplyBiquad(ADS1292R_BiquadState *state,
                                  float input)
{
    float output;

    output = (state->b0 * input)
           + (state->b1 * state->x1)
           + (state->b2 * state->x2)
           - (state->a1 * state->y1)
           - (state->a2 * state->y2);

    state->x2 = state->x1;
    state->x1 = input;
    state->y2 = state->y1;
    state->y1 = output;

    return output;
}

void ADS1292R_CH2FilterInit(ADS1292R_CH2FilterState *filter)
{
    memset(filter, 0, sizeof(*filter));

    /* Second-order notch, f0 = 60 Hz, Q = 30, fs = 500 Hz. */
    filter->notch.b0 =  0.98758894f;
    filter->notch.b1 = -1.43984271f;
    filter->notch.b2 =  0.98758894f;
    filter->notch.a1 = -1.43984271f;
    filter->notch.a2 =  0.97517788f;

    /* Second-order Butterworth high-pass, fc = 0.5 Hz, fs = 500 Hz. */
    filter->high_pass.b0 =  0.995566972018f;
    filter->high_pass.b1 = -1.991133944040f;
    filter->high_pass.b2 =  0.995566972018f;
    filter->high_pass.a1 = -1.991114292200f;
    filter->high_pass.a2 =  0.991153595869f;

    /* Second-order Butterworth low-pass, fc = 40 Hz, fs = 500 Hz. */
    filter->low_pass.b0 = 0.0461318020933f;
    filter->low_pass.b1 = 0.0922636041866f;
    filter->low_pass.b2 = 0.0461318020933f;
    filter->low_pass.a1 = -1.3072850288500f;
    filter->low_pass.a2 = 0.4918122372230f;
}

float ADS1292R_ProcessCH2Sample(ADS1292R_CH2FilterState *filter,
                               int32_t ch2_raw)
{
    float input = (float)ch2_raw;
    float notched;
    float high_passed;

    /*
     * Prime the unity-DC-gain notch at steady state, then prime only the
     * high-pass input delays with the electrode DC level.  The high-pass
     * output delays and every low-pass delay remain zero.  This avoids a
     * large start-up step while preserving every raw sample for logging.
     */
    if (filter->initialized == 0U)
    {
        filter->notch.x1 = input;
        filter->notch.x2 = input;
        filter->notch.y1 = input;
        filter->notch.y2 = input;
        filter->high_pass.x1 = input;
        filter->high_pass.x2 = input;
        filter->initialized = 1U;
    }

    notched = ADS1292R_ApplyBiquad(&filter->notch, input);
    high_passed = ADS1292R_ApplyBiquad(&filter->high_pass, notched);
    return ADS1292R_ApplyBiquad(&filter->low_pass, high_passed);
}
