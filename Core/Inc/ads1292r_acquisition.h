#ifndef INC_ADS1292R_ACQUISITION_H_
#define INC_ADS1292R_ACQUISITION_H_

#include "tx_api.h"
#include <stdint.h>

#define ADS1292R_USB_BATCH_SIZE 10U

typedef struct
{
  uint64_t timestamp_us;
  uint32_t sample_counter;
  uint32_t drdy_counter;
  uint32_t status;
  int32_t ch1_raw;
  int32_t ch2_raw;
  float ch2_filtered;
  uint32_t error_flags;
} ADS1292R_Record;

UINT ADS1292R_AcquisitionInit(VOID *memory_ptr);
void ADS1292R_DRDY_EXTI_Callback(void);
void ADS1292R_USB_Activated(void);
void ADS1292R_USB_Deactivated(void);
UINT ADS1292R_WaitAndPopBatch(
    ADS1292R_Record records[ADS1292R_USB_BATCH_SIZE]);
uint32_t ADS1292R_ProfileNow(void);

/* Diagnostic counter rules:
 * drdy: every DRDY EXTI; sample: every valid record published; overrun: edge
 * rejected/replaced while busy/pending; bad_status: invalid 0xCxxxxx prefix;
 * spi_error: non-HAL_OK 9-byte read; buffer_overflow: oldest record discarded;
 * usb_error: format/write/short-write failure; usb_reconnect: CDC activation. */
extern volatile uint32_t ads_drdy_count;
extern volatile uint32_t ads_sample_count;
extern volatile uint32_t ads_overrun_count;
extern volatile uint32_t ads_bad_status_count;
extern volatile uint32_t ads_spi_error_count;
extern volatile uint32_t ads_buffer_overflow_count;
extern volatile uint32_t ads_usb_error_count;
extern volatile uint32_t ads_usb_reconnect_count;

extern volatile uint32_t ads_latency_us_last;
extern volatile uint32_t ads_latency_us_max;
extern volatile uint32_t ads_cs_duration_us_last;
extern volatile uint32_t ads_cs_duration_us_max;
extern volatile uint32_t ads_acquisition_us_last;
extern volatile uint32_t ads_acquisition_us_max;
extern volatile uint32_t ads_usb_format_us_last;
extern volatile uint32_t ads_usb_format_us_max;
extern volatile uint32_t ads_usb_write_us_last;
extern volatile uint32_t ads_usb_write_us_max;

#endif /* INC_ADS1292R_ACQUISITION_H_ */
