/*
 * max30205.c
 *
 * MAX30205 driver on I2C1, interrupt-driven HAL transfers completed through
 * a ThreadX semaphore. See max30205.h.
 */

#include "max30205.h"

#include "main.h"
#include "i2c.h"
#include "tx_api.h"


/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/* HAL expects the 7-bit address shifted left by one */
#define MAX30205_HAL_ADDRESS        ((uint16_t)(MAX30205_I2C_ADDRESS_7BIT << 1))

/*
 * Completion timeout. A 4-byte transfer takes about 0.4 ms at 100 kHz;
 * 100 ms also covers clock stretching and the sensor's 50 ms bus timeout.
 */
#define MAX30205_TRANSFER_TIMEOUT_TICKS  \
    ((ULONG)((100U * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U))

/* Shutdown, comparator mode, active-low OS, fault queue 1, normal format,
   bus timeout enabled */
#define MAX30205_CONFIG_IDLE        MAX30205_CFG_SHUTDOWN

/* Half period of the recovery clock (about 5 us at 80 MHz, not critical) */
#define MAX30205_RECOVERY_DELAY_LOOPS  200U
#define MAX30205_RECOVERY_CLOCKS       9U


/* -------------------------------------------------------------------------- */
/* Debug variables                                                            */
/* -------------------------------------------------------------------------- */

volatile uint32_t max30205_transfer_ok_count = 0U;
volatile uint32_t max30205_transfer_error_count = 0U;
volatile uint32_t max30205_transfer_timeout_count = 0U;
volatile uint32_t max30205_bus_recovery_count = 0U;
volatile uint32_t max30205_last_hal_error = 0U;
volatile uint8_t  max30205_config_readback = 0U;


/* -------------------------------------------------------------------------- */
/* Private variables                                                          */
/* -------------------------------------------------------------------------- */

static TX_SEMAPHORE max30205_done_semaphore;
static uint8_t max30205_semaphore_created = 0U;

/* Result of the last transfer, written by the HAL callbacks */
static volatile uint8_t max30205_transfer_failed = 0U;

/* DMA-free IT transfers read / write these buffers after the call returns,
   so they must outlive the calling function */
static uint8_t max30205_tx_buffer[2];
static uint8_t max30205_rx_buffer[2];


/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

/* Drop completions left over from a transfer that previously timed out */
static void MAX30205_DrainSemaphore(void)
{
    while (tx_semaphore_get(&max30205_done_semaphore, TX_NO_WAIT) == TX_SUCCESS)
    {
    }
}


/* Wait for the completion of the transfer that was just started */
static MAX30205_Status MAX30205_WaitDone(HAL_StatusTypeDef start_status)
{
    if (start_status != HAL_OK)
    {
        max30205_last_hal_error = hi2c1.ErrorCode;
        max30205_transfer_error_count++;
        return MAX30205_ERROR;
    }

    if (tx_semaphore_get(&max30205_done_semaphore,
                         MAX30205_TRANSFER_TIMEOUT_TICKS) != TX_SUCCESS)
    {
        max30205_transfer_timeout_count++;
        return MAX30205_TIMEOUT;
    }

    if (max30205_transfer_failed != 0U)
    {
        max30205_last_hal_error = hi2c1.ErrorCode;
        max30205_transfer_error_count++;

        return ((hi2c1.ErrorCode & HAL_I2C_ERROR_AF) != 0U) ?
               MAX30205_NOT_FOUND : MAX30205_ERROR;
    }

    max30205_transfer_ok_count++;
    return MAX30205_OK;
}


static MAX30205_Status MAX30205_WriteRegister8(uint8_t reg, uint8_t value)
{
    MAX30205_DrainSemaphore();
    max30205_transfer_failed = 0U;
    max30205_tx_buffer[0] = value;

    return MAX30205_WaitDone(
            HAL_I2C_Mem_Write_IT(&hi2c1,
                                 MAX30205_HAL_ADDRESS,
                                 reg,
                                 I2C_MEMADD_SIZE_8BIT,
                                 max30205_tx_buffer,
                                 1U));
}


static MAX30205_Status MAX30205_ReadRegister(uint8_t reg, uint16_t length)
{
    MAX30205_DrainSemaphore();
    max30205_transfer_failed = 0U;

    return MAX30205_WaitDone(
            HAL_I2C_Mem_Read_IT(&hi2c1,
                                MAX30205_HAL_ADDRESS,
                                reg,
                                I2C_MEMADD_SIZE_8BIT,
                                max30205_rx_buffer,
                                length));
}


static void MAX30205_RecoveryDelay(void)
{
    for (volatile uint32_t i = 0U; i < MAX30205_RECOVERY_DELAY_LOOPS; i++)
    {
        __NOP();
    }
}


/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

MAX30205_Status MAX30205_Init(void)
{
    MAX30205_Status status;

    if (max30205_semaphore_created == 0U)
    {
        if (tx_semaphore_create(&max30205_done_semaphore,
                                "max30205_done", 0U) != TX_SUCCESS)
        {
            return MAX30205_ERROR;
        }
        max30205_semaphore_created = 1U;
    }

    /* Shutdown: the sensor only converts on one-shot requests */
    status = MAX30205_WriteRegister8(MAX30205_REG_CONFIG, MAX30205_CONFIG_IDLE);
    if (status != MAX30205_OK)
    {
        return status;
    }

    /* Read back to confirm the device answers with the expected value */
    status = MAX30205_ReadRegister(MAX30205_REG_CONFIG, 1U);
    if (status != MAX30205_OK)
    {
        return status;
    }

    max30205_config_readback = max30205_rx_buffer[0];

    if ((max30205_config_readback & (uint8_t)~MAX30205_CFG_ONE_SHOT) !=
        MAX30205_CONFIG_IDLE)
    {
        return MAX30205_ERROR;
    }

    return MAX30205_OK;
}


MAX30205_Status MAX30205_StartOneShot(void)
{
    return MAX30205_WriteRegister8(MAX30205_REG_CONFIG,
                                   MAX30205_CONFIG_IDLE | MAX30205_CFG_ONE_SHOT);
}


MAX30205_Status MAX30205_ReadRaw(int16_t *raw)
{
    MAX30205_Status status;

    status = MAX30205_ReadRegister(MAX30205_REG_TEMPERATURE, 2U);
    if (status != MAX30205_OK)
    {
        return status;
    }

    /* MSB first */
    *raw = (int16_t)(((uint16_t)max30205_rx_buffer[0] << 8) |
                     (uint16_t)max30205_rx_buffer[1]);

    return MAX30205_OK;
}


void MAX30205_RecoverBus(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t i;

    max30205_bus_recovery_count++;

    /* Also disables the I2C1 interrupts and releases PB6 / PB7 */
    (void)HAL_I2C_DeInit(&hi2c1);

    /* SCL and SDA as open-drain outputs, released (high via the pull-ups) */
    HAL_GPIO_WritePin(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin, GPIO_PIN_SET);

    gpio.Pin = I2C1_SCL_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(I2C1_SCL_GPIO_Port, &gpio);

    gpio.Pin = I2C1_SDA_Pin;
    HAL_GPIO_Init(I2C1_SDA_GPIO_Port, &gpio);

    /* Clock until the slave releases SDA (it finishes the byte it is sending) */
    for (i = 0U; i < MAX30205_RECOVERY_CLOCKS; i++)
    {
        if (HAL_GPIO_ReadPin(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin) == GPIO_PIN_SET)
        {
            break;
        }

        HAL_GPIO_WritePin(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin, GPIO_PIN_RESET);
        MAX30205_RecoveryDelay();
        HAL_GPIO_WritePin(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin, GPIO_PIN_SET);
        MAX30205_RecoveryDelay();
    }

    /* STOP: SDA low -> high while SCL is high */
    HAL_GPIO_WritePin(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin, GPIO_PIN_RESET);
    MAX30205_RecoveryDelay();
    HAL_GPIO_WritePin(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin, GPIO_PIN_SET);
    MAX30205_RecoveryDelay();
    HAL_GPIO_WritePin(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin, GPIO_PIN_SET);
    MAX30205_RecoveryDelay();

    HAL_GPIO_DeInit(I2C1_SCL_GPIO_Port, I2C1_SCL_Pin);
    HAL_GPIO_DeInit(I2C1_SDA_GPIO_Port, I2C1_SDA_Pin);

    /* Back to I2C1 (MspInit restores the pins and the interrupts) */
    MX_I2C1_Init();
}


/* -------------------------------------------------------------------------- */
/* HAL callbacks (override the weak ones). I2C1 is used by this driver only.  */
/* -------------------------------------------------------------------------- */

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &hi2c1)
    {
        (void)tx_semaphore_put(&max30205_done_semaphore);
    }
}


void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &hi2c1)
    {
        (void)tx_semaphore_put(&max30205_done_semaphore);
    }
}


void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &hi2c1)
    {
        max30205_transfer_failed = 1U;
        (void)tx_semaphore_put(&max30205_done_semaphore);
    }
}


void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &hi2c1)
    {
        max30205_transfer_failed = 1U;
        (void)tx_semaphore_put(&max30205_done_semaphore);
    }
}
