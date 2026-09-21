/*
 * icm20948.c
 *
 *  Created on: 2026. 9. 16.
 *      Author: Minwoo Kim
 */


#include "icm20948.h"
#include "main.h"
extern SPI_HandleTypeDef hspi3;

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
        HAL_SPI_Transmit(&hspi3,
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
        HAL_SPI_TransmitReceive(&hspi3,
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
HAL_StatusTypeDef ICM20948_Init(void)
{
    HAL_StatusTypeDef status;
    uint8_t who_am_i = 0;
    uint8_t readback = 0;

    /* SPI idle state */
    ICM20948_CS_HIGH();
    HAL_Delay(10);

    /* Confirm communication */
    status = ICM20948_ReadWhoAmI(&who_am_i);

    if (status != HAL_OK)
    {
        return status;
    }

    if (who_am_i != ICM20948_WHO_AM_I_VALUE)
    {
        return HAL_ERROR;
    }

    /* Software reset: User Bank 0, PWR_MGMT_1[7] */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    status = ICM20948_WriteRegister(
        ICM20948_REG_PWR_MGMT_1,
        ICM20948_DEVICE_RESET);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Reset takes time and resets the active register bank.
     * After reset, the device returns to sleep mode.
     */
    HAL_Delay(100);

    /* Wake up and use the best available internal clock */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    status = ICM20948_WriteRegister(
        ICM20948_REG_PWR_MGMT_1,
        ICM20948_CLK_AUTO);

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(10);

    /*
     * Enable all accelerometer and gyroscope axes.
     * 0x00 means no axes are disabled.
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_PWR_MGMT_2,
        0x00);

    if (status != HAL_OK)
    {
        return status;
    }

    /* Verify wake-up configuration */
    status = ICM20948_ReadRegister(
        ICM20948_REG_PWR_MGMT_1,
        &readback);

    if (status != HAL_OK)
    {
        return status;
    }

    if ((readback & 0x41U) != ICM20948_CLK_AUTO)
    {
        return HAL_ERROR;
    }

    status = ICM20948_ConfigureAccelerometerSCG();

    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_OK;
}

HAL_StatusTypeDef ICM20948_ConfigureAccelerometerSCG(void)
{
    HAL_StatusTypeDef status;
    uint8_t readback = 0;
    uint16_t divider = ICM20948_ACCEL_SMPLRT_DIV_SCG;

    /*
     * User Bank 0:
     * Enable all accelerometer axes and disable all gyroscope axes.
     *
     * PWR_MGMT_2:
     * bits [5:3] = accelerometer disable bits → 000: all enabled
     * bits [2:0] = gyroscope disable bits     → 111: all disabled
     */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    status = ICM20948_WriteRegister(
        ICM20948_REG_PWR_MGMT_2,
        0x07);

    if (status != HAL_OK)
    {
        return status;
    }

    /* Accelerometer configuration registers are in User Bank 2 */
    status = ICM20948_SelectBank(2);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Accelerometer ODR:
     * ODR = 1125 / (1 + divider)
     *
     * divider = 1:
     * ODR = 1125 / 2 = 562.5 samples/s
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_ACCEL_SMPLRT_DIV_1,
        (uint8_t)((divider >> 8) & 0x0FU));

    if (status != HAL_OK)
    {
        return status;
    }

    status = ICM20948_WriteRegister(
        ICM20948_REG_ACCEL_SMPLRT_DIV_2,
        (uint8_t)(divider & 0xFFU));

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * ±2 g, DLPF enabled, approximately 111 Hz bandwidth
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_ACCEL_CONFIG,
        ICM20948_ACCEL_CONFIG_SCG);

    if (status != HAL_OK)
    {
        return status;
    }

    /* Verify ACCEL_CONFIG */
    status = ICM20948_ReadRegister(
        ICM20948_REG_ACCEL_CONFIG,
        &readback);

    if (status != HAL_OK)
    {
        return status;
    }

    if (readback != ICM20948_ACCEL_CONFIG_SCG)
    {
        return HAL_ERROR;
    }

    /* Leave the device in User Bank 0 */
    return ICM20948_SelectBank(0);
}

HAL_StatusTypeDef ICM20948_ReadAccelRaw(
    ICM20948_AccelRaw *accel)
{
    HAL_StatusTypeDef status;

    uint8_t tx_data[7] = {0};
    uint8_t rx_data[7] = {0};

    if (accel == NULL)
    {
        return HAL_ERROR;
    }

    /*
     * Accelerometer output registers are in User Bank 0.
     */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * First byte: register address with read bit set.
     * Remaining six bytes generate SPI clocks for XYZ data.
     */
    tx_data[0] = ICM20948_REG_ACCEL_XOUT_H | 0x80U;

    ICM20948_CS_LOW();

    status = HAL_SPI_TransmitReceive(
        &hspi3,
        tx_data,
        rx_data,
        sizeof(tx_data),
        100);

    ICM20948_CS_HIGH();

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Each accelerometer axis is big-endian signed 16-bit.
     */
    accel->x = (int16_t)(
        ((uint16_t)rx_data[1] << 8) |
         (uint16_t)rx_data[2]);

    accel->y = (int16_t)(
        ((uint16_t)rx_data[3] << 8) |
         (uint16_t)rx_data[4]);

    accel->z = (int16_t)(
        ((uint16_t)rx_data[5] << 8) |
         (uint16_t)rx_data[6]);

    return HAL_OK;
}


