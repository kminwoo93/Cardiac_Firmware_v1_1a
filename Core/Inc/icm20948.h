/*
 * icm20948.h
 *
 *  Created on: 2026. 9. 16.
 *      Author: Minwoo Kim
 */

#ifndef INC_ICM20948_H_
#define INC_ICM20948_H_

#include "main.h"

#define ICM20948_REG_BANK_SEL       0x7F

/* User Bank 0 */
#define ICM20948_REG_WHO_AM_I       0x00
#define ICM20948_REG_PWR_MGMT_1     0x06
#define ICM20948_REG_PWR_MGMT_2     0x07
/* ICM-20948 User Bank 0 interrupt registers */
#define ICM20948_REG_INT_PIN_CFG       0x0FU
#define ICM20948_REG_INT_ENABLE_1      0x11U
#define ICM20948_REG_INT_STATUS_1      0x1AU

#define ICM20948_WHO_AM_I_VALUE     0xEA

#define ICM20948_DEVICE_RESET       0x80
#define ICM20948_CLK_AUTO           0x01

/* User Bank 2 */
#define ICM20948_REG_GYRO_SMPLRT_DIV       0x00
#define ICM20948_REG_GYRO_CONFIG_1         0x01
#define ICM20948_REG_ACCEL_SMPLRT_DIV_1    0x10
#define ICM20948_REG_ACCEL_SMPLRT_DIV_2    0x11
#define ICM20948_REG_ACCEL_CONFIG          0x14

/* PWR_MGMT_2: 0x00 = all accelerometer and gyroscope axes enabled */
#define ICM20948_PWR_MGMT_2_ALL_ON         0x00U

/* SCG configuration */
#define ICM20948_ACCEL_SMPLRT_DIV_SCG      1U

/*
 * Gyroscope ODR = 1100 / (1 + divider) = 550 Hz.
 * The accelerometer runs at 1125 / (1 + 1) = 562.5 Hz. No divider pair gives
 * the same rate above 25 Hz, so the gyroscope is read together with the
 * accelerometer on each accelerometer Data Ready: about 2 % of the gyroscope
 * values are repeated.
 */
#define ICM20948_GYRO_SMPLRT_DIV_SCG       1U

/*
 * Output registers, User Bank 0.
 * ACCEL_XOUT_H .. ACCEL_ZOUT_L (0x2D..0x32) are followed by
 * GYRO_XOUT_H .. GYRO_ZOUT_L (0x33..0x38): one 12-byte burst reads both.
 */
#define ICM20948_REG_ACCEL_XOUT_H    0x2D
#define ICM20948_REG_GYRO_XOUT_H     0x33
#define ICM20948_IMU_DATA_BYTES      12U

/* INT_ENABLE_1 register bit 0 */
#define ICM20948_RAW_DATA_0_RDY_EN     0x01U
/* INT_STATUS_1 register bit 0 */
#define ICM20948_RAW_DATA_0_RDY_INT    0x01U

/* One accelerometer + gyroscope sample, raw signed 16-bit counts */
typedef struct
{
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} ICM20948_ImuRaw;

HAL_StatusTypeDef ICM20948_ReadImuRaw(
    ICM20948_ImuRaw *imu);

/*
 * ACCEL_CONFIG
 * DLPFCFG = 1: approximately 111 Hz bandwidth
 * FS_SEL  = 0: ±2 g
 * FCHOICE = 1: DLPF enabled
 */
#define ICM20948_ACCEL_CONFIG_SCG          0x09U

/*
 * GYRO_CONFIG_1
 * bits [5:3] GYRO_DLPFCFG = 2: approximately 119.5 Hz bandwidth
 *            (closest to the accelerometer 111 Hz)
 * bits [2:1] GYRO_FS_SEL  = 0: ±250 dps (131 LSB per dps)
 * bit  [0]   GYRO_FCHOICE = 1: DLPF enabled
 */
#define ICM20948_GYRO_CONFIG_1_SCG         0x11U

HAL_StatusTypeDef ICM20948_Init(void);

HAL_StatusTypeDef ICM20948_WriteRegister(uint8_t reg,
                                        uint8_t value);

HAL_StatusTypeDef ICM20948_ReadRegister(uint8_t reg,
                                       uint8_t *value);

HAL_StatusTypeDef ICM20948_SelectBank(uint8_t bank);

HAL_StatusTypeDef ICM20948_ReadWhoAmI(uint8_t *who_am_i);

HAL_StatusTypeDef ICM20948_ConfigureImuSCG(void);

HAL_StatusTypeDef ICM20948_EnableDataReadyInterrupt(void);

#endif /* INC_ICM20948_H_ */
