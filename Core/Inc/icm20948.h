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
#define ICM20948_REG_WHO_AM_I       0x00
#define ICM20948_WHO_AM_I_VALUE     0xEA

HAL_StatusTypeDef ICM20948_WriteRegister(uint8_t reg,
                                        uint8_t value);

HAL_StatusTypeDef ICM20948_ReadRegister(uint8_t reg,
                                       uint8_t *value);

HAL_StatusTypeDef ICM20948_SelectBank(uint8_t bank);

HAL_StatusTypeDef ICM20948_ReadWhoAmI(uint8_t *who_am_i);



#endif /* INC_ICM20948_H_ */
