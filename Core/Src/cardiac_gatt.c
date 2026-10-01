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

/*
 * ECG / SCG stream packets.
 *
 * Largest ATT payload the U5 builds. The Transparent Mode firmware limits
 * the ATT MTU to 160 (157-byte payload); 244 matches an ATT MTU of 247.
 */
#define CARDIAC_STREAM_MAX_PAYLOAD         244U
#define CARDIAC_STREAM_HEADER_SIZE         12U

/* Send a partly filled packet once its first sample is this old */
#define CARDIAC_STREAM_FLUSH_MS            200U

/* Limits per Cardiac_GATT_Process() call, so HCI events keep flowing */
#define CARDIAC_STREAM_MAX_PACKETS_PER_PROCESS   8U
#define CARDIAC_STREAM_MAX_DISCARD_PER_PROCESS   128U

/* Header format byte: high nibble = version, low nibble = channel mask */
#define CARDIAC_STREAM_FORMAT_VERSION      1U
#define CARDIAC_ECG_CHANNEL_MASK           0x02U   /* CH2 only */
#define CARDIAC_SCG_AXIS_MASK              0x07U   /* X, Y, Z */


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

/* ECG / SCG stream statistics */
volatile uint32_t cardiac_gatt_ecg_packet_count = 0;
volatile uint32_t cardiac_gatt_ecg_sample_count = 0;
volatile uint32_t cardiac_gatt_ecg_error_count = 0;
volatile uint32_t cardiac_gatt_ecg_gap_count = 0;
volatile uint32_t cardiac_gatt_scg_packet_count = 0;
volatile uint32_t cardiac_gatt_scg_sample_count = 0;
volatile uint32_t cardiac_gatt_scg_error_count = 0;
volatile uint32_t cardiac_gatt_scg_gap_count = 0;

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

/* One ECG or SCG sample in a common form */
typedef struct
{
    uint32_t counter;
    uint32_t timestamp;
    int32_t  value[3];
} Cardiac_StreamSample;

/*
 * Packet assembly state of one stream.
 *
 * Packet layout, little-endian:
 *  0  u16  sequence number (per stream, wraps)
 *  2  u32  sample_counter of the first sample
 *  6  u32  timestamp in us of the first sample (low 32 bits of TIM2)
 * 10  u8   number of samples
 * 11  u8   format: version << 4 | channel mask
 * 12  ...  samples (ECG: int24 per channel, SCG: int16 X, Y, Z)
 *
 * Samples in one packet are consecutive (sample_counter + 1 each).
 */
typedef struct
{
    uint8_t  (*read)(Cardiac_StreamSample *sample);
    void     (*pack)(uint8_t *dst, const Cardiac_StreamSample *sample);
    volatile uint16_t *char_handle;
    volatile uint32_t *packet_count;
    volatile uint32_t *sample_count;
    volatile uint32_t *error_count;
    volatile uint32_t *gap_count;
    uint8_t  cccd_bit;
    uint8_t  bytes_per_sample;
    uint8_t  format;

    uint16_t seq;
    uint8_t  buffer[CARDIAC_STREAM_MAX_PAYLOAD];
    uint8_t  n_samples;
    uint8_t  ready;
    uint32_t next_counter;
    uint32_t first_tick;

    /* Sample read from the queue that starts the next packet */
    Cardiac_StreamSample carry;
    uint8_t  has_carry;
} Cardiac_Stream;

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


/* -------------------------------------------------------------------------- */
/* ECG / SCG streams                                                          */
/* -------------------------------------------------------------------------- */

static uint8_t Cardiac_GATT_ReadEcg(Cardiac_StreamSample *sample)
{
    ECG_ProcessingSample message;

    if (tx_queue_receive(&ble_ecg_queue, &message, TX_NO_WAIT) != TX_SUCCESS)
    {
        return 0U;
    }

    sample->counter = message.sample_counter;
    sample->timestamp = message.timestamp_low;
    sample->value[0] = message.ecg_raw;

    return 1U;
}


static uint8_t Cardiac_GATT_ReadScg(Cardiac_StreamSample *sample)
{
    BLE_SCGSample message;

    if (tx_queue_receive(&ble_scg_queue, &message, TX_NO_WAIT) != TX_SUCCESS)
    {
        return 0U;
    }

    sample->counter = message.sample_counter;
    sample->timestamp = message.timestamp_low;
    sample->value[0] = message.accel_x_raw;
    sample->value[1] = message.accel_y_raw;
    sample->value[2] = message.accel_z_raw;

    return 1U;
}


/* ECG CH2: signed 24-bit (ADS1292R resolution) */
static void Cardiac_GATT_PackEcg(uint8_t *dst, const Cardiac_StreamSample *sample)
{
    uint32_t v = (uint32_t)sample->value[0];

    dst[0] = (uint8_t)(v & 0xFFU);
    dst[1] = (uint8_t)((v >> 8) & 0xFFU);
    dst[2] = (uint8_t)((v >> 16) & 0xFFU);
}


/* SCG: signed 16-bit X, Y, Z */
static void Cardiac_GATT_PackScg(uint8_t *dst, const Cardiac_StreamSample *sample)
{
    Cardiac_GATT_PutU16(&dst[0], (uint16_t)sample->value[0]);
    Cardiac_GATT_PutU16(&dst[2], (uint16_t)sample->value[1]);
    Cardiac_GATT_PutU16(&dst[4], (uint16_t)sample->value[2]);
}


static Cardiac_Stream cardiac_ecg_stream =
{
    .read = Cardiac_GATT_ReadEcg,
    .pack = Cardiac_GATT_PackEcg,
    .char_handle = &cardiac_gatt_ecg_char_handle,
    .packet_count = &cardiac_gatt_ecg_packet_count,
    .sample_count = &cardiac_gatt_ecg_sample_count,
    .error_count = &cardiac_gatt_ecg_error_count,
    .gap_count = &cardiac_gatt_ecg_gap_count,
    .cccd_bit = CARDIAC_GATT_CCCD_ECG,
    .bytes_per_sample = 3U,
    .format = (CARDIAC_STREAM_FORMAT_VERSION << 4) | CARDIAC_ECG_CHANNEL_MASK,
};

static Cardiac_Stream cardiac_scg_stream =
{
    .read = Cardiac_GATT_ReadScg,
    .pack = Cardiac_GATT_PackScg,
    .char_handle = &cardiac_gatt_scg_char_handle,
    .packet_count = &cardiac_gatt_scg_packet_count,
    .sample_count = &cardiac_gatt_scg_sample_count,
    .error_count = &cardiac_gatt_scg_error_count,
    .gap_count = &cardiac_gatt_scg_gap_count,
    .cccd_bit = CARDIAC_GATT_CCCD_SCG,
    .bytes_per_sample = 6U,
    .format = (CARDIAC_STREAM_FORMAT_VERSION << 4) | CARDIAC_SCG_AXIS_MASK,
};


static void Cardiac_GATT_StreamReset(Cardiac_Stream *stream)
{
    stream->n_samples = 0U;
    stream->ready = 0U;
    stream->has_carry = 0U;
}


/* Samples that fit one notification with the current ATT MTU */
static uint8_t Cardiac_GATT_StreamCapacity(const Cardiac_Stream *stream)
{
    uint16_t payload = (cardiac_gatt_att_mtu > 3U) ?
                       (uint16_t)(cardiac_gatt_att_mtu - 3U) : 0U;
    uint16_t samples;

    if (payload > CARDIAC_STREAM_MAX_PAYLOAD)
    {
        payload = CARDIAC_STREAM_MAX_PAYLOAD;
    }

    if (payload <= CARDIAC_STREAM_HEADER_SIZE)
    {
        return 0U;
    }

    samples = (uint16_t)((payload - CARDIAC_STREAM_HEADER_SIZE) /
                         stream->bytes_per_sample);

    return (samples > 255U) ? 255U : (uint8_t)samples;
}


/* Fill the packet; returns 1 when it is ready to send */
static uint8_t Cardiac_GATT_StreamFill(Cardiac_Stream *stream, uint8_t capacity)
{
    Cardiac_StreamSample sample;

    while (stream->n_samples < capacity)
    {
        if (stream->has_carry != 0U)
        {
            sample = stream->carry;
            stream->has_carry = 0U;
        }
        else if (stream->read(&sample) == 0U)
        {
            break;
        }

        /* A gap (dropped samples) ends the packet */
        if ((stream->n_samples > 0U) &&
            (sample.counter != stream->next_counter))
        {
            (*stream->gap_count)++;
            stream->carry = sample;
            stream->has_carry = 1U;
            return 1U;
        }

        if (stream->n_samples == 0U)
        {
            Cardiac_GATT_PutU32(&stream->buffer[2], sample.counter);
            Cardiac_GATT_PutU32(&stream->buffer[6], sample.timestamp);
            stream->first_tick = HAL_GetTick();
        }

        stream->pack(&stream->buffer[CARDIAC_STREAM_HEADER_SIZE +
                                     ((uint16_t)stream->n_samples *
                                      stream->bytes_per_sample)],
                     &sample);
        stream->n_samples++;
        stream->next_counter = sample.counter + 1U;
    }

    if (stream->n_samples == 0U)
    {
        return 0U;
    }

    if ((stream->n_samples >= capacity) ||
        ((HAL_GetTick() - stream->first_tick) >= CARDIAC_STREAM_FLUSH_MS))
    {
        return 1U;
    }

    return 0U;
}


static void Cardiac_GATT_StreamProcess(Cardiac_Stream *stream)
{
    Cardiac_StreamSample sample;
    uint8_t capacity;
    uint8_t ret;
    uint32_t count;

    if ((cardiac_gatt_connected == 0U) ||
        ((cardiac_gatt_cccd_mask & stream->cccd_bit) == 0U))
    {
        /* Not subscribed: empty what is left in the queue */
        Cardiac_GATT_StreamReset(stream);

        for (count = 0U; count < CARDIAC_STREAM_MAX_DISCARD_PER_PROCESS; count++)
        {
            if (stream->read(&sample) == 0U)
            {
                break;
            }
        }
        return;
    }

    capacity = Cardiac_GATT_StreamCapacity(stream);

    if (capacity == 0U)
    {
        return;
    }

    for (count = 0U; count < CARDIAC_STREAM_MAX_PACKETS_PER_PROCESS; count++)
    {
        if (stream->ready == 0U)
        {
            stream->ready = Cardiac_GATT_StreamFill(stream, capacity);

            if (stream->ready == 0U)
            {
                return;
            }
        }

        Cardiac_GATT_PutU16(&stream->buffer[0], stream->seq);
        stream->buffer[10] = stream->n_samples;
        stream->buffer[11] = stream->format;

        ret = Cardiac_GATT_Notify(*stream->char_handle,
                                  stream->cccd_bit,
                                  stream->buffer,
                                  (uint16_t)(CARDIAC_STREAM_HEADER_SIZE +
                                             ((uint16_t)stream->n_samples *
                                              stream->bytes_per_sample)));

        if ((ret == BLE_STATUS_INSUFFICIENT_RESOURCES) ||
            (ret == BLE_STATUS_BUSY))
        {
            /* WB05N TX buffers full: retry the same packet later */
            return;
        }

        if (ret == BLE_STATUS_SUCCESS)
        {
            (*stream->packet_count)++;
            (*stream->sample_count) += stream->n_samples;
        }
        else
        {
            (*stream->error_count)++;
        }

        /* The sequence number also advances on errors, so gaps are visible */
        stream->seq++;
        stream->n_samples = 0U;
        stream->ready = 0U;
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
    Cardiac_GATT_StreamReset(&cardiac_ecg_stream);
    Cardiac_GATT_StreamReset(&cardiac_scg_stream);
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
    Cardiac_GATT_StreamProcess(&cardiac_ecg_stream);
    Cardiac_GATT_StreamProcess(&cardiac_scg_stream);
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
