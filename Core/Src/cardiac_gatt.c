#include "cardiac_gatt.h"

#include <string.h>

#include "main.h"
#include "app_threadx.h"

#include "ble_status.h"
#include "hci_parser.h"
#include "stm32wb05n_gatt_aci.h"
#include "stm32wb05n_gatt_server.h"


/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/*
 * 128-bit UUIDs, little-endian as required by the ACI.
 * Base: 3c01xxxx-9c91-433a-b7fd-3900ec745671, xxxx at bytes 12-13.
 */
#define CARDIAC_UUID_SERVICE      0x0001U
#define CARDIAC_UUID_ECG          0x0002U
#define CARDIAC_UUID_SCG          0x0003U
#define CARDIAC_UUID_RPEAK        0x0004U
#define CARDIAC_UUID_STATUS       0x0005U

/*
 * Attribute handles reserved for the service:
 * 1 service declaration + 4 characteristics x (declaration, value, CCCD),
 * plus a margin for later characteristics (Control, ECG CH1, ...).
 */
#define CARDIAC_SERVICE_MAX_ATTR_RECORDS   20U

/*
 * Value storage reserved in the STM32WB05N GATT database.
 *
 * Notify-only characteristics are never read, and notifications carry their
 * payload in the command itself, so only a minimal value is stored. The
 * GATT database and the advertising data share a small buffer on the WB05N
 * (CFG_BLE_GATT_ADV_NWK_BUFFER_SIZE in the Transparent Mode firmware).
 */
#define CARDIAC_NOTIFY_ONLY_VALUE_LEN      1U

/* ATT bearer used for notifications (unenhanced ATT) */
#define CARDIAC_ATT_CID                    0x0004U

/* Client Characteristic Configuration: notifications enabled bit */
#define CARDIAC_CCCD_NOTIFY_BIT            0x01U

#define CARDIAC_ATT_MTU_DEFAULT            23U

#define CARDIAC_STATUS_PERIOD_MS           1000U

/* R-peak notification payload (fits the default 23-byte ATT MTU) */
#define CARDIAC_RPEAK_PAYLOAD_SIZE         20U

/* Maximum R-peak events taken from the queue per Cardiac_GATT_Process() */
#define CARDIAC_RPEAK_MAX_PER_PROCESS      8U


/* -------------------------------------------------------------------------- */
/* Debug variables                                                            */
/* -------------------------------------------------------------------------- */

/*
 * cardiac_gatt_init_status:
 * 0xFF = not registered yet
 * 0x00 = BLE_STATUS_SUCCESS
 * other value = error code of the failing aci_gatt_srv_* call
 */
volatile uint8_t  cardiac_gatt_init_status = 0xFF;

volatile uint8_t  cardiac_gatt_cccd_mask = 0;
volatile uint16_t cardiac_gatt_att_mtu = CARDIAC_ATT_MTU_DEFAULT;
volatile uint32_t cardiac_gatt_notify_ok_count = 0;
volatile uint32_t cardiac_gatt_notify_fail_count = 0;
volatile uint8_t  cardiac_gatt_last_notify_status = BLE_STATUS_SUCCESS;

/* R-peak notifications */
volatile uint32_t cardiac_gatt_rpeak_sent_count = 0;
/* Events discarded because no client subscribed to R-peak */
volatile uint32_t cardiac_gatt_rpeak_skipped_count = 0;
/* Events discarded after a notification error other than "buffers full" */
volatile uint32_t cardiac_gatt_rpeak_error_count = 0;

/* Attribute handles (characteristic declaration handles) */
volatile uint16_t cardiac_gatt_service_handle = 0;
volatile uint16_t cardiac_gatt_ecg_char_handle = 0;
volatile uint16_t cardiac_gatt_scg_char_handle = 0;
volatile uint16_t cardiac_gatt_rpeak_char_handle = 0;
volatile uint16_t cardiac_gatt_status_char_handle = 0;


/* -------------------------------------------------------------------------- */
/* Private variables                                                          */
/* -------------------------------------------------------------------------- */

/* Defined in hci_tl_interface.c */
extern volatile uint32_t hci_tl_uart_rx_restart_count;

static const uint8_t cardiac_uuid_base[16] =
{
    0x71, 0x56, 0x74, 0xEC, 0x00, 0x39, 0xFD, 0xB7,
    0x3A, 0x43, 0x91, 0x9C, 0x00, 0x00, 0x01, 0x3C
};

static volatile uint8_t  cardiac_gatt_connected = 0;
static volatile uint16_t cardiac_gatt_conn_handle = 0;

static uint32_t cardiac_gatt_last_status_tick = 0;

/* R-peak waiting for free WB05N TX buffers */
static uint8_t  cardiac_gatt_rpeak_pending[CARDIAC_RPEAK_PAYLOAD_SIZE];
static uint8_t  cardiac_gatt_rpeak_has_pending = 0;


/* -------------------------------------------------------------------------- */
/* Private functions                                                          */
/* -------------------------------------------------------------------------- */

static void Cardiac_GATT_MakeUuid(uint16_t short_uuid, uint8_t uuid[16])
{
    memcpy(uuid, cardiac_uuid_base, 16);
    uuid[12] = (uint8_t)(short_uuid & 0xFFU);
    uuid[13] = (uint8_t)(short_uuid >> 8);
}


static uint8_t Cardiac_GATT_AddChar(uint16_t short_uuid,
                                    uint16_t value_len,
                                    uint8_t properties,
                                    uint16_t *char_handle)
{
    Char_UUID_t uuid;

    Cardiac_GATT_MakeUuid(short_uuid, uuid.Char_UUID_128);

    return aci_gatt_srv_add_char_nwk(
            cardiac_gatt_service_handle,
            UUID_TYPE_128,
            &uuid,
            value_len,
            properties,
            ATTR_PERMISSION_NONE,
            GATT_DONT_NOTIFY_EVENTS,
            0x07,
            CHAR_VALUE_LEN_VARIABLE,
            char_handle);
}


static void Cardiac_GATT_PutU16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)(value >> 8);
}


static void Cardiac_GATT_PutU32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)(value >> 24);
}


static uint16_t Cardiac_GATT_Saturate16(uint32_t value)
{
    return (value > 0xFFFFU) ? 0xFFFFU : (uint16_t)value;
}


/*
 * Status payload, little-endian (version 1, 20 bytes):
 *
 *  0  u8   version
 *  1  u8   CCCD mask (bit0 ECG, bit1 SCG, bit2 R-peak, bit3 Status)
 *  2  u16  ATT MTU
 *  4  u32  uptime in ms
 *  8  u32  notifications sent
 * 12  u32  notifications failed
 * 16  u16  HCI events dropped on the U5 (saturating)
 * 18  u16  UART RX restarts on the U5 (saturating)
 */
static void Cardiac_GATT_BuildStatus(uint8_t buffer[CARDIAC_GATT_STATUS_SIZE])
{
    buffer[0] = CARDIAC_GATT_STATUS_VERSION;
    buffer[1] = cardiac_gatt_cccd_mask;
    Cardiac_GATT_PutU16(&buffer[2], cardiac_gatt_att_mtu);
    Cardiac_GATT_PutU32(&buffer[4], HAL_GetTick());
    Cardiac_GATT_PutU32(&buffer[8], cardiac_gatt_notify_ok_count);
    Cardiac_GATT_PutU32(&buffer[12], cardiac_gatt_notify_fail_count);
    Cardiac_GATT_PutU16(&buffer[16],
                        Cardiac_GATT_Saturate16(hci_dropped_packet_count));
    Cardiac_GATT_PutU16(&buffer[18],
                        Cardiac_GATT_Saturate16(hci_tl_uart_rx_restart_count));
}


static uint8_t Cardiac_GATT_Notify(uint16_t char_handle,
                                   uint8_t cccd_bit,
                                   uint8_t *data,
                                   uint16_t length)
{
    tBleStatus ret;

    if ((cardiac_gatt_connected == 0U) ||
        ((cardiac_gatt_cccd_mask & cccd_bit) == 0U))
    {
        return BLE_STATUS_NOT_ALLOWED;
    }

    ret = aci_gatt_srv_notify(cardiac_gatt_conn_handle,
                              CARDIAC_ATT_CID,
                              char_handle + 1U,
                              GATT_NOTIFICATION,
                              length,
                              data);

    cardiac_gatt_last_notify_status = ret;

    if (ret == BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_notify_ok_count++;
    }
    else
    {
        cardiac_gatt_notify_fail_count++;
    }

    return ret;
}


/*
 * R-peak payload, little-endian (20 bytes):
 *
 *  0  u32  sample_counter of the R peak (ECG sample index)
 *  4  u32  timestamp in us (low 32 bits of the TIM2 time base)
 *  8  i32  ECG CH2 raw amplitude at the peak
 * 12  u32  RR interval in us (0 for the first beat)
 * 16  u16  heart rate in bpm (0 for the first beat)
 * 18  u16  confidence, Q15
 */
static void Cardiac_GATT_BuildRPeak(const ECG_RPeakEvent *event,
                                    uint8_t buffer[CARDIAC_RPEAK_PAYLOAD_SIZE])
{
    Cardiac_GATT_PutU32(&buffer[0], event->sample_counter);
    Cardiac_GATT_PutU32(&buffer[4], event->timestamp_low);
    Cardiac_GATT_PutU32(&buffer[8], (uint32_t)event->amplitude);
    Cardiac_GATT_PutU32(&buffer[12], event->rr_interval_us);
    Cardiac_GATT_PutU16(&buffer[16],
                        Cardiac_GATT_Saturate16(event->heart_rate_bpm));
    Cardiac_GATT_PutU16(&buffer[18],
                        Cardiac_GATT_Saturate16(event->confidence_q15));
}


/*
 * Returns 1 when the payload was consumed (sent or discarded),
 * 0 when the WB05N TX buffers are full and it must be retried later.
 */
static uint8_t Cardiac_GATT_SendRPeak(uint8_t payload[CARDIAC_RPEAK_PAYLOAD_SIZE])
{
    uint8_t ret;

    if ((cardiac_gatt_connected == 0U) ||
        ((cardiac_gatt_cccd_mask & CARDIAC_GATT_CCCD_RPEAK) == 0U))
    {
        cardiac_gatt_rpeak_skipped_count++;
        return 1U;
    }

    ret = Cardiac_GATT_Notify(cardiac_gatt_rpeak_char_handle,
                              CARDIAC_GATT_CCCD_RPEAK,
                              payload,
                              CARDIAC_RPEAK_PAYLOAD_SIZE);

    if (ret == BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_rpeak_sent_count++;
        return 1U;
    }

    if ((ret == BLE_STATUS_INSUFFICIENT_RESOURCES) ||
        (ret == BLE_STATUS_BUSY))
    {
        return 0U;
    }

    cardiac_gatt_rpeak_error_count++;
    return 1U;
}


static void Cardiac_GATT_ProcessRPeaks(void)
{
    ECG_RPeakEvent event;
    uint32_t count;

    if (cardiac_gatt_rpeak_has_pending != 0U)
    {
        if (Cardiac_GATT_SendRPeak(cardiac_gatt_rpeak_pending) == 0U)
        {
            return;
        }
        cardiac_gatt_rpeak_has_pending = 0U;
    }

    for (count = 0U; count < CARDIAC_RPEAK_MAX_PER_PROCESS; count++)
    {
        if (tx_queue_receive(&ble_rpeak_queue, &event, TX_NO_WAIT) !=
            TX_SUCCESS)
        {
            return;
        }

        Cardiac_GATT_BuildRPeak(&event, cardiac_gatt_rpeak_pending);

        if (Cardiac_GATT_SendRPeak(cardiac_gatt_rpeak_pending) == 0U)
        {
            cardiac_gatt_rpeak_has_pending = 1U;
            return;
        }
    }
}


static void Cardiac_GATT_UpdateStatus(void)
{
    uint8_t status[CARDIAC_GATT_STATUS_SIZE];

    Cardiac_GATT_BuildStatus(status);

    /* Latest value for Read requests */
    (void)aci_gatt_srv_write_handle_value_nwk(
            cardiac_gatt_status_char_handle + 1U,
            0,
            CARDIAC_GATT_STATUS_SIZE,
            status);

    (void)Cardiac_GATT_Notify(cardiac_gatt_status_char_handle,
                              CARDIAC_GATT_CCCD_STATUS,
                              status,
                              CARDIAC_GATT_STATUS_SIZE);
}


/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

uint8_t Cardiac_GATT_Init(void)
{
    tBleStatus ret;
    Service_UUID_t service_uuid;
    uint16_t handle;

    Cardiac_GATT_MakeUuid(CARDIAC_UUID_SERVICE, service_uuid.Service_UUID_128);

    ret = aci_gatt_srv_add_service_nwk(
            UUID_TYPE_128,
            &service_uuid,
            PRIMARY_SERVICE,
            CARDIAC_SERVICE_MAX_ATTR_RECORDS,
            &handle);

    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_init_status = ret;
        return ret;
    }

    cardiac_gatt_service_handle = handle;

    ret = Cardiac_GATT_AddChar(CARDIAC_UUID_ECG,
                               CARDIAC_NOTIFY_ONLY_VALUE_LEN,
                               CHAR_PROP_NOTIFY,
                               &handle);
    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_init_status = ret;
        return ret;
    }
    cardiac_gatt_ecg_char_handle = handle;

    ret = Cardiac_GATT_AddChar(CARDIAC_UUID_SCG,
                               CARDIAC_NOTIFY_ONLY_VALUE_LEN,
                               CHAR_PROP_NOTIFY,
                               &handle);
    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_init_status = ret;
        return ret;
    }
    cardiac_gatt_scg_char_handle = handle;

    ret = Cardiac_GATT_AddChar(CARDIAC_UUID_RPEAK,
                               CARDIAC_NOTIFY_ONLY_VALUE_LEN,
                               CHAR_PROP_NOTIFY,
                               &handle);
    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_init_status = ret;
        return ret;
    }
    cardiac_gatt_rpeak_char_handle = handle;

    ret = Cardiac_GATT_AddChar(CARDIAC_UUID_STATUS,
                               CARDIAC_GATT_STATUS_SIZE,
                               CHAR_PROP_READ | CHAR_PROP_NOTIFY,
                               &handle);
    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_gatt_init_status = ret;
        return ret;
    }
    cardiac_gatt_status_char_handle = handle;

    /* Readable from the start */
    Cardiac_GATT_UpdateStatus();
    cardiac_gatt_last_status_tick = HAL_GetTick();

    cardiac_gatt_init_status = BLE_STATUS_SUCCESS;

    return BLE_STATUS_SUCCESS;
}


void Cardiac_GATT_OnConnected(uint16_t connection_handle)
{
    cardiac_gatt_conn_handle = connection_handle;
    cardiac_gatt_cccd_mask = 0U;
    cardiac_gatt_att_mtu = CARDIAC_ATT_MTU_DEFAULT;
    cardiac_gatt_connected = 1U;
}


void Cardiac_GATT_OnDisconnected(void)
{
    cardiac_gatt_connected = 0U;
    cardiac_gatt_rpeak_has_pending = 0U;
    cardiac_gatt_cccd_mask = 0U;
    cardiac_gatt_att_mtu = CARDIAC_ATT_MTU_DEFAULT;
}


void Cardiac_GATT_Process(void)
{
    if (cardiac_gatt_init_status != BLE_STATUS_SUCCESS)
    {
        return;
    }

    if ((HAL_GetTick() - cardiac_gatt_last_status_tick) >=
        CARDIAC_STATUS_PERIOD_MS)
    {
        cardiac_gatt_last_status_tick = HAL_GetTick();
        Cardiac_GATT_UpdateStatus();
    }

    Cardiac_GATT_ProcessRPeaks();
}


/* -------------------------------------------------------------------------- */
/* ACI event handlers (override the weak ones in the middleware)              */
/* -------------------------------------------------------------------------- */

void aci_gatt_srv_attribute_modified_event(uint16_t Connection_Handle,
                                           uint16_t CID,
                                           uint16_t Attr_Handle,
                                           uint16_t Attr_Data_Length,
                                           uint8_t Attr_Data[])
{
    uint8_t bit = 0U;

    if (Attr_Data_Length == 0U)
    {
        return;
    }

    /* CCCD = characteristic declaration handle + 2 */
    if (Attr_Handle == (uint16_t)(cardiac_gatt_ecg_char_handle + 2U))
    {
        bit = CARDIAC_GATT_CCCD_ECG;
    }
    else if (Attr_Handle == (uint16_t)(cardiac_gatt_scg_char_handle + 2U))
    {
        bit = CARDIAC_GATT_CCCD_SCG;
    }
    else if (Attr_Handle == (uint16_t)(cardiac_gatt_rpeak_char_handle + 2U))
    {
        bit = CARDIAC_GATT_CCCD_RPEAK;
    }
    else if (Attr_Handle == (uint16_t)(cardiac_gatt_status_char_handle + 2U))
    {
        bit = CARDIAC_GATT_CCCD_STATUS;
    }

    if (bit == 0U)
    {
        return;
    }

    if ((Attr_Data[0] & CARDIAC_CCCD_NOTIFY_BIT) != 0U)
    {
        cardiac_gatt_cccd_mask |= bit;
    }
    else
    {
        cardiac_gatt_cccd_mask &= (uint8_t)~bit;
    }
}


void aci_att_exchange_mtu_resp_event(uint16_t Connection_Handle,
                                     uint16_t MTU)
{
    cardiac_gatt_att_mtu = MTU;
}
