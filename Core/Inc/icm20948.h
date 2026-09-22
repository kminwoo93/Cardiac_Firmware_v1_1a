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
#define ICM20948_REG_ACCEL_SMPLRT_DIV_1    0x10
#define ICM20948_REG_ACCEL_SMPLRT_DIV_2    0x11
#define ICM20948_REG_ACCEL_CONFIG          0x14

/* SCG configuration */
#define ICM20948_ACCEL_SMPLRT_DIV_SCG      1U

#define ICM20948_REG_ACCEL_XOUT_H    0x2D

/* INT_ENABLE_1 register bit 0 */
#define ICM20948_RAW_DATA_0_RDY_EN     0x01U
/* INT_STATUS_1 register bit 0 */
#define ICM20948_RAW_DATA_0_RDY_INT    0x01U

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} ICM20948_AccelRaw;

HAL_StatusTypeDef ICM20948_ReadAccelRaw(
    ICM20948_AccelRaw *accel);

/*
 * ACCEL_CONFIG
 * DLPFCFG = 1: approximately 111 Hz bandwidth
 * FS_SEL  = 0: ±2 g
 * FCHOICE = 1: DLPF enabled
 */
#define ICM20948_ACCEL_CONFIG_SCG          0x09U

HAL_StatusTypeDef ICM20948_Init(void);

HAL_StatusTypeDef ICM20948_WriteRegister(uint8_t reg,
                                        uint8_t value);

HAL_StatusTypeDef ICM20948_ReadRegister(uint8_t reg,
                                       uint8_t *value);

HAL_StatusTypeDef ICM20948_SelectBank(uint8_t bank);

HAL_StatusTypeDef ICM20948_ReadWhoAmI(uint8_t *who_am_i);

HAL_StatusTypeDef ICM20948_ConfigureAccelerometerSCG(void);

HAL_StatusTypeDef ICM20948_EnableDataReadyInterrupt(void);

#endif /* INC_ICM20948_H_ */
