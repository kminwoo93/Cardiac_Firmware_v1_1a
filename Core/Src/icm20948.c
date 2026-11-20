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

    status = ICM20948_ConfigureImuSCG();

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Start Data Ready interrupt generation only after
     * the accelerometer and gyroscope configuration is complete.
     */
    status = ICM20948_EnableDataReadyInterrupt();

    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_OK;
}

HAL_StatusTypeDef ICM20948_ConfigureImuSCG(void)
{
    HAL_StatusTypeDef status;
    uint8_t readback = 0;
    uint16_t divider = ICM20948_ACCEL_SMPLRT_DIV_SCG;

    /*
     * User Bank 0:
     * Enable all accelerometer and gyroscope axes.
     *
     * PWR_MGMT_2:
     * bits [5:3] = accelerometer disable bits → 000: all enabled
     * bits [2:0] = gyroscope disable bits     → 000: all enabled
     */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    status = ICM20948_WriteRegister(
        ICM20948_REG_PWR_MGMT_2,
        ICM20948_PWR_MGMT_2_ALL_ON);

    if (status != HAL_OK)
    {
        return status;
    }

    status = ICM20948_ReadRegister(
        ICM20948_REG_PWR_MGMT_2,
        &readback);

    if (status != HAL_OK)
    {
        return status;
    }

    if ((readback & 0x3FU) != ICM20948_PWR_MGMT_2_ALL_ON)
    {
        return HAL_ERROR;
    }

    /* Accelerometer and gyroscope configuration registers are in User Bank 2 */
    status = ICM20948_SelectBank(2);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Gyroscope ODR:
     * ODR = 1100 / (1 + divider) = 550 samples/s
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_GYRO_SMPLRT_DIV,
        (uint8_t)ICM20948_GYRO_SMPLRT_DIV_SCG);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * ±250 dps, DLPF enabled, approximately 119.5 Hz bandwidth
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_GYRO_CONFIG_1,
        ICM20948_GYRO_CONFIG_1_SCG);

    if (status != HAL_OK)
    {
        return status;
    }

    /* Verify GYRO_CONFIG_1 */
    status = ICM20948_ReadRegister(
        ICM20948_REG_GYRO_CONFIG_1,
        &readback);

    if (status != HAL_OK)
    {
        return status;
    }

    if ((readback & 0x3FU) != ICM20948_GYRO_CONFIG_1_SCG)
    {
        return HAL_ERROR;
    }

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

/* Big-endian signed 16-bit value from two output register bytes */
static int16_t ICM20948_ToInt16(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

HAL_StatusTypeDef ICM20948_ReadImuRaw(
    ICM20948_ImuRaw *imu)
{
    HAL_StatusTypeDef status;

    uint8_t tx_data[1U + ICM20948_IMU_DATA_BYTES] = {0};
    uint8_t rx_data[1U + ICM20948_IMU_DATA_BYTES] = {0};

    if (imu == NULL)
    {
        return HAL_ERROR;
    }

    /*
     * Accelerometer and gyroscope output registers are in User Bank 0.
     */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * First byte: register address with read bit set.
     * The remaining 12 bytes generate SPI clocks for
     * accelerometer XYZ (0x2D..0x32) and gyroscope XYZ (0x33..0x38).
     * One burst keeps the six values from the same register snapshot.
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
     * Each axis is big-endian signed 16-bit.
     */
    imu->accel_x = ICM20948_ToInt16(&rx_data[1]);
    imu->accel_y = ICM20948_ToInt16(&rx_data[3]);
    imu->accel_z = ICM20948_ToInt16(&rx_data[5]);

    imu->gyro_x = ICM20948_ToInt16(&rx_data[7]);
    imu->gyro_y = ICM20948_ToInt16(&rx_data[9]);
    imu->gyro_z = ICM20948_ToInt16(&rx_data[11]);

    return HAL_OK;
}

HAL_StatusTypeDef ICM20948_EnableDataReadyInterrupt(void)
{
    HAL_StatusTypeDef status;
    uint8_t readback = 0U;

    /*
     * Interrupt registers are in User Bank 0.
     */
    status = ICM20948_SelectBank(0);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * INT_PIN_CFG = 0x00
     *
     * INT1 active high
     * Push-pull output
     * Pulse mode
     * Interrupt status cleared by reading INT_STATUS
     *
     * This matches the STM32 PB5 rising-edge EXTI setting.
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_INT_PIN_CFG,
        0x00U);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Generate an INT1 pulse whenever new raw sensor
     * data is ready.
     */
    status = ICM20948_WriteRegister(
        ICM20948_REG_INT_ENABLE_1,
        ICM20948_RAW_DATA_0_RDY_EN);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Verify that RAW_DATA_0_RDY interrupt was enabled.
     */
    status = ICM20948_ReadRegister(
        ICM20948_REG_INT_ENABLE_1,
        &readback);

    if (status != HAL_OK)
    {
        return status;
    }

    if ((readback & ICM20948_RAW_DATA_0_RDY_EN) == 0U)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}
