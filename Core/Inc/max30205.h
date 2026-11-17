/*
 * max30205.h
 *
 * MAX30205 human body temperature sensor on I2C1 (PB6 = SCL, PB7 = SDA).
 *
 * The sensor is kept in shutdown and measured in one-shot mode:
 *   MAX30205_StartOneShot() -> wait MAX30205_CONVERSION_TIME_MS
 *   -> MAX30205_ReadRaw().
 *
 * Transfers use the HAL interrupt API. The calling thread blocks on a
 * ThreadX semaphore until the transfer completes, so it uses no CPU while
 * the bus is busy. Call these functions from one thread only (the I2C1 bus
 * is owned by the temperature thread), never from an ISR.
 */

#ifndef INC_MAX30205_H_
#define INC_MAX30205_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 7-bit address with A0 = A1 = A2 = GND */
#define MAX30205_I2C_ADDRESS_7BIT    0x48U

/* Registers */
#define MAX30205_REG_TEMPERATURE     0x00U
#define MAX30205_REG_CONFIG          0x01U
#define MAX30205_REG_THYST           0x02U
#define MAX30205_REG_TOS             0x03U

/* Configuration register bits */
#define MAX30205_CFG_SHUTDOWN        0x01U
#define MAX30205_CFG_COMP_INT        0x02U
#define MAX30205_CFG_OS_POLARITY     0x04U
#define MAX30205_CFG_FAULT_QUEUE     0x18U
#define MAX30205_CFG_DATA_FORMAT     0x20U  /* 1 = extended (+64 C offset) */
#define MAX30205_CFG_TIMEOUT_DIS     0x40U
#define MAX30205_CFG_ONE_SHOT        0x80U

/* Conversion time: 44 ms typical, 50 ms maximum */
#define MAX30205_CONVERSION_TIME_MS  50U

/* Temperature LSB: 1/256 C (0.00390625 C), normal data format */
#define MAX30205_LSB_DIVIDER         256

typedef enum
{
    MAX30205_OK = 0,
    MAX30205_ERROR,      /* HAL refused the transfer or reported a bus error */
    MAX30205_TIMEOUT,    /* no completion callback within the timeout */
    MAX30205_NOT_FOUND   /* no ACK at the configured address */
} MAX30205_Status;

/* Debug variables (Live Expressions) */
extern volatile uint32_t max30205_transfer_ok_count;
extern volatile uint32_t max30205_transfer_error_count;
extern volatile uint32_t max30205_transfer_timeout_count;
extern volatile uint32_t max30205_bus_recovery_count;
extern volatile uint32_t max30205_last_hal_error;   /* hi2c1.ErrorCode */
extern volatile uint8_t  max30205_config_readback;

/*
 * Probe the sensor and put it in shutdown (ready for one-shot).
 * Must be called from a thread (creates the completion semaphore once).
 */
MAX30205_Status MAX30205_Init(void);

/* Start one conversion; the result is ready after MAX30205_CONVERSION_TIME_MS */
MAX30205_Status MAX30205_StartOneShot(void);

/* Read the temperature register (two's complement, 1/256 C per LSB) */
MAX30205_Status MAX30205_ReadRaw(int16_t *raw);

/*
 * Reset I2C1 after an error: de-initialize it, clock SCL up to 9 times to
 * release a slave holding SDA low, send a STOP and initialize it again.
 */
void MAX30205_RecoverBus(void);

/* Raw value to milli-degrees Celsius */
static inline int32_t MAX30205_RawToMilliCelsius(int16_t raw)
{
    return ((int32_t)raw * 1000) / MAX30205_LSB_DIVIDER;
}

#ifdef __cplusplus
}
#endif

#endif /* INC_MAX30205_H_ */
