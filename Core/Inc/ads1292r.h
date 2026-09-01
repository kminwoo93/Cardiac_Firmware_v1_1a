/*
 * ads1292r.h
 *
 *  Created on: Aug 31, 2026
 *      Author: Minwoo Kim
 */

#ifndef INC_ADS1292R_H_
#define INC_ADS1292R_H_

#include "main.h"
#include <stdint.h>

/*
 * ADS1292R Command Definitions
 */

#define ADS1292R_CMD_WAKEUP      0x02
#define ADS1292R_CMD_STANDBY     0x04
#define ADS1292R_CMD_RESET       0x06
#define ADS1292R_CMD_START       0x08
#define ADS1292R_CMD_STOP        0x0A

#define ADS1292R_CMD_RDATAC      0x10
#define ADS1292R_CMD_SDATAC      0x11
#define ADS1292R_CMD_RDATA       0x12

#define ADS1292R_CMD_RREG        0x20
#define ADS1292R_CMD_WREG        0x40


/*
 * ADS1292R Register Addresses
 */

#define ADS1292R_REG_ID          0x00
#define ADS1292R_REG_CONFIG1     0x01
#define ADS1292R_REG_CONFIG2     0x02
#define ADS1292R_REG_LOFF        0x03
#define ADS1292R_REG_CH1SET      0x04
#define ADS1292R_REG_CH2SET      0x05
#define ADS1292R_REG_RLD_SENS    0x06
#define ADS1292R_REG_LOFF_SENS   0x07
#define ADS1292R_REG_LOFF_STAT   0x08
#define ADS1292R_REG_RESP1       0x09
#define ADS1292R_REG_RESP2       0x0A
#define ADS1292R_REG_GPIO        0x0B


/*
 * Public Functions
 */

void ADS1292R_SendCommand(uint8_t command);

uint8_t ADS1292R_ReadRegister(uint8_t address);

void ADS1292R_WriteRegister(uint8_t address, uint8_t value);

void ADS1292R_HardwareReset(void);

void ADS1292R_ReadData(uint8_t *data);

int32_t ADS1292R_Convert24Bit(uint8_t b0,
                              uint8_t b1,
                              uint8_t b2);

#endif /* INC_ADS1292R_H_ */
