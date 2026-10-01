/*
 * cardiac_ble.h
 *
 *  Created on: 2026. 9. 30.
 *      Author: Minwoo Kim
 */

#ifndef INC_CARDIAC_BLE_H_
#define INC_CARDIAC_BLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* BLE initialization result.
 * 0x00 = BLE_STATUS_SUCCESS
 */
extern volatile uint8_t cardiac_ble_init_status;
extern volatile uint8_t cardiac_ble_stage;

/* 1 while a central is connected */
extern volatile uint8_t  cardiac_ble_connected;
extern volatile uint16_t cardiac_ble_conn_handle;

/* Initialize the STM32WB05N, the BLE stack and start advertising.
 * Must be called from the BLE thread (uses tx_thread_sleep()).
 */
void Cardiac_BLE_Init(void);

/* Process pending HCI events.
 * Must be called continuously from the BLE thread.
 */
void Cardiac_BLE_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_CARDIAC_BLE_H_ */
