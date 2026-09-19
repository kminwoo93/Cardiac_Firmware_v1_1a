/*
 * icm20948.c
 *
 *  Created on: 2026. 9. 16.
 *      Author: Minwoo Kim
 */


#include "icm20948.h"
#include "spi.h"

#define ICM20948_CS_LOW()                                      \
    HAL_GPIO_WritePin(ICM_CS_GPIO_Port,                        \
                      ICM_CS_Pin,                              \
                      GPIO_PIN_RESET)

#define ICM20948_CS_HIGH()                                     \
    HAL_GPIO_WritePin(ICM_CS_GPIO_Port,                        \
                      ICM_CS_Pin,                              \
                      GPIO_PIN_SET)

HAL_StatusTypeDef ICM20948_WriteRegister(uint8_t reg,
                                        uint8_t value)
{
    uint8_t tx_data[2];

    tx_data[0] = reg & 0x7F;  /* Bit 7 = 0: write */
    tx_data[1] = value;

    ICM20948_CS_LOW();

    HAL_StatusTypeDef status =
        HAL_SPI_Transmit(&hspi2,
                         tx_data,
                         2,
                         100);

    ICM20948_CS_HIGH();

    return status;
}


HAL_StatusTypeDef ICM20948_ReadRegister(uint8_t reg,
                                       uint8_t *value)
{
    uint8_t tx_data[2];
    uint8_t rx_data[2];

    if (value == NULL)
    {
        return HAL_ERROR;
    }

    tx_data[0] = reg | 0x80;  /* Bit 7 = 1: read */
    tx_data[1] = 0x00;        /* Dummy byte */

    rx_data[0] = 0x00;
    rx_data[1] = 0x00;

    ICM20948_CS_LOW();

    HAL_StatusTypeDef status =
        HAL_SPI_TransmitReceive(&hspi2,
                                tx_data,
                                rx_data,
                                2,
                                100);

    ICM20948_CS_HIGH();

    if (status == HAL_OK)
    {
        *value = rx_data[1];
    }

    return status;
}


HAL_StatusTypeDef ICM20948_SelectBank(uint8_t bank)
{
    if (bank > 3)
    {
        return HAL_ERROR;
    }

    return ICM20948_WriteRegister(
        ICM20948_REG_BANK_SEL,
        (uint8_t)(bank << 4));
}


HAL_StatusTypeDef ICM20948_ReadWhoAmI(uint8_t *who_am_i)
{
    HAL_StatusTypeDef status;

    /* WHO_AM_I is in register bank 0 */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    return ICM20948_ReadRegister(
        ICM20948_REG_WHO_AM_I,
        who_am_i);
}
