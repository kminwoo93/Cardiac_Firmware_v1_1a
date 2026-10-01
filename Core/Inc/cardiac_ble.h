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
#include "tx_api.h"

/* BLE initialization result.
 * 0x00 = BLE_STATUS_SUCCESS
 */
extern volatile uint8_t cardiac_ble_init_status;
extern volatile uint8_t cardiac_ble_stage;

/* 1 while a central is connected */
extern volatile uint8_t  cardiac_ble_connected;
extern volatile uint16_t cardiac_ble_conn_handle;

/* Initialize STM32WB05N HCI transport and test communication with HCI Reset. */
void Cardiac_BLE_Init(void);

/* Process pending HCI events.
 * Called from the BLE thread.
 */
void Cardiac_BLE_Process(void);

/* Create the BLE thread that processes HCI events.
 * Call from App_ThreadX_Init().
 */
UINT Cardiac_BLE_ThreadCreate(TX_BYTE_POOL *byte_pool);

/* Signal the BLE thread that HCI events were received.
 * Safe to call from interrupt context.
 */
void Cardiac_BLE_RxNotify(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_CARDIAC_BLE_H_ */
