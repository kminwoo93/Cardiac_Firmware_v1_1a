/*
 * cardiac_gatt.h
 *
 * Cardiac GATT service running on the STM32WB05N (network processor).
 *
 * Service UUID : 3c010001-9c91-433a-b7fd-3900ec745671
 *   ECG Raw    : 3c010002-...  Notify
 *   SCG Raw    : 3c010003-...  Notify
 *   R-peak     : 3c010004-...  Notify
 *   Status     : 3c010005-...  Read, Notify (once per second)
 */

#ifndef INC_CARDIAC_GATT_H_
#define INC_CARDIAC_GATT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Bits of cardiac_gatt_cccd_mask: notifications enabled by the client */
#define CARDIAC_GATT_CCCD_ECG       0x01U
#define CARDIAC_GATT_CCCD_SCG       0x02U
#define CARDIAC_GATT_CCCD_RPEAK     0x04U
#define CARDIAC_GATT_CCCD_STATUS    0x08U

/* Status characteristic payload size (fits the default 23-byte ATT MTU) */
#define CARDIAC_GATT_STATUS_SIZE    20U
#define CARDIAC_GATT_STATUS_VERSION 1U

/* Debug variables */
extern volatile uint8_t  cardiac_gatt_init_status;
extern volatile uint8_t  cardiac_gatt_cccd_mask;
extern volatile uint16_t cardiac_gatt_att_mtu;
extern volatile uint32_t cardiac_gatt_notify_ok_count;
extern volatile uint32_t cardiac_gatt_notify_fail_count;
extern volatile uint8_t  cardiac_gatt_last_notify_status;
extern volatile uint32_t cardiac_gatt_rpeak_sent_count;
extern volatile uint32_t cardiac_gatt_rpeak_skipped_count;
extern volatile uint32_t cardiac_gatt_rpeak_error_count;
extern volatile uint32_t cardiac_gatt_ecg_packet_count;
extern volatile uint32_t cardiac_gatt_ecg_sample_count;
extern volatile uint32_t cardiac_gatt_ecg_error_count;
extern volatile uint32_t cardiac_gatt_ecg_gap_count;
extern volatile uint32_t cardiac_gatt_scg_packet_count;
extern volatile uint32_t cardiac_gatt_scg_sample_count;
extern volatile uint32_t cardiac_gatt_scg_error_count;
extern volatile uint32_t cardiac_gatt_scg_gap_count;

/* Register the Cardiac service and its characteristics.
 * Call after aci_gatt_srv_profile_init() and aci_gap_profile_init().
 */
uint8_t Cardiac_GATT_Init(void);

/* Connection state changes, called from the HCI event handlers */
void Cardiac_GATT_OnConnected(uint16_t connection_handle);
void Cardiac_GATT_OnDisconnected(void);

/* Periodic work (Status update, R-peak, ECG and SCG notifications).
 * Called from the BLE thread.
 */
void Cardiac_GATT_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_CARDIAC_GATT_H_ */
