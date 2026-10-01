#include "cardiac_ble.h"
#include "cardiac_gatt.h"

#include "hci.h"
#include "hci_tl.h"
#include "hci_const.h"
#include "hci_parser.h"

#include "stm32wb05n_aci.h"
#include "stm32wb05n_hci_le.h"
#include "stm32wb05n_events.h"
#include "stm32wb05n_gap.h"
#include "stm32wb05n_gap_aci.h"
#include "stm32wb05n_gatt_aci.h"
#include "stm32wb05n_l2cap_aci.h"

#include "tx_api.h"


/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define CARDIAC_BDADDR_SIZE                  6U

/* Same Service Changed configuration used by ST SensorDemo */
#define CARDIAC_GATT_SERVICE_CHANGED_BIT     0x01U

/*
 * Advertising interval:
 * 0x00A0 = 160 units
 * BLE advertising interval unit = 0.625 ms
 * -> approximately 100 ms
 */
#define CARDIAC_ADV_INTERVAL_MIN             0x00A0U
#define CARDIAC_ADV_INTERVAL_MAX             0x00A0U

/* Maximum time to wait for ACI_BLUE_INITIALIZED after the hardware reset */
#define CARDIAC_BLE_BOOT_TIMEOUT_MS          2000U

/* 2 s stabilization delay after HCI_Reset (same as ST SensorDemo) */
#define CARDIAC_BLE_STABILIZATION_TICKS      (2U * TX_TIMER_TICKS_PER_SECOND)

/*
 * Link optimization requested after each connection.
 *
 * Data length: 251-byte LL payload so that one notification (up to
 * MTU - 3 = 157 bytes) fits in a single radio packet instead of 6.
 * 2120 us is the time of a 251-byte packet on the 1M PHY.
 */
#define CARDIAC_LINK_TX_OCTETS               251U
#define CARDIAC_LINK_TX_TIME_US              2120U

/* Preferred PHY: 2M for TX and RX (0x02 = LE 2M bit) */
#define CARDIAC_LINK_PHY_2M                  0x02U

/*
 * Connection parameters (Apple accessory guidelines compliant):
 * interval 15 - 30 ms (unit 1.25 ms), no peripheral latency,
 * supervision timeout 4 s (unit 10 ms).
 */
#define CARDIAC_LINK_CONN_INTERVAL_MIN       12U
#define CARDIAC_LINK_CONN_INTERVAL_MAX       24U
#define CARDIAC_LINK_PERIPHERAL_LATENCY      0U
#define CARDIAC_LINK_SUPERVISION_TIMEOUT     400U

/* Delays after the connection (ms): let the phone finish service
   discovery and its own MTU / PHY procedures first */
#define CARDIAC_LINK_MTU_DELAY_MS            1000U
#define CARDIAC_LINK_CONN_PARAM_DELAY_MS     2000U
#define CARDIAC_LINK_READ_PHY_DELAY_MS       3000U
#define CARDIAC_LINK_RETRY_MS                500U
#define CARDIAC_LINK_MAX_RETRIES             3U

/* Default ATT MTU before any exchange */
#define CARDIAC_LINK_DEFAULT_ATT_MTU         23U



/* -------------------------------------------------------------------------- */
/* Debug variables                                                            */
/* -------------------------------------------------------------------------- */

/*
 * cardiac_ble_init_status:
 *
 * 0xFF = initialization not completed
 * 0x00 = BLE_STATUS_SUCCESS
 * other value = BLE/HCI error code
 */
volatile uint8_t cardiac_ble_init_status = 0xFF;
volatile uint8_t  cardiac_ble_hci_version = 0;
volatile uint16_t cardiac_ble_hci_revision = 0;
volatile uint8_t  cardiac_ble_lmp_version = 0;
volatile uint16_t cardiac_ble_manufacturer = 0;
volatile uint16_t cardiac_ble_lmp_subversion = 0;
volatile uint8_t  cardiac_ble_version_status = 0xFF;

/*
 * Useful for debugger:
 *
 * 0  = not started
 * 1  = HCI transport initialized
 * 2  = HCI Reset successful
 * 3  = BLE address configured
 * 4  = GATT initialized
 * 5  = GAP initialized
 * 6  = GAP Peripheral profile initialized
 * 7  = Device name written (Cardiac GATT service registered next,
 *      see cardiac_gatt_init_status)
 * 8  = Advertising configured
 * 9  = Advertising data configured
 * 10 = Advertising enabled
 */
volatile uint8_t cardiac_ble_stage = 0;

/* Set by ACI_BLUE_INITIALIZED: STM32WB05N finished booting */
volatile uint8_t  cardiac_ble_boot_done = 0;
volatile uint8_t  cardiac_ble_boot_reason = 0;

/* Connection state */
volatile uint8_t  cardiac_ble_connected = 0;
volatile uint16_t cardiac_ble_conn_handle = 0;
volatile uint8_t  cardiac_ble_disconnect_reason = 0;
volatile uint32_t cardiac_ble_connection_count = 0;

/* Last aci_gap_set_advertising_enable() status after a disconnection */
volatile uint8_t  cardiac_ble_adv_restart_status = 0;

/*
 * Link parameters of the current connection.
 * PHY: 1 = 1M, 2 = 2M, 3 = Coded. 0 = unknown.
 */
volatile uint32_t cardiac_ble_conn_interval_us = 0;
volatile uint16_t cardiac_ble_conn_latency = 0;
volatile uint16_t cardiac_ble_supervision_timeout_ms = 0;
volatile uint8_t  cardiac_ble_tx_phy = 0;
volatile uint8_t  cardiac_ble_rx_phy = 0;
volatile uint16_t cardiac_ble_max_tx_octets = 0;
volatile uint16_t cardiac_ble_max_rx_octets = 0;

/* Status of each link optimization command (0xFF = not sent yet) */
volatile uint8_t  cardiac_ble_event_mask_status = 0xFF;
volatile uint8_t  cardiac_ble_default_dle_status = 0xFF;
volatile uint8_t  cardiac_ble_default_phy_status = 0xFF;
volatile uint8_t  cardiac_ble_dle_status = 0xFF;
volatile uint8_t  cardiac_ble_phy_status = 0xFF;
volatile uint8_t  cardiac_ble_phy_update_status = 0xFF;
volatile uint8_t  cardiac_ble_mtu_req_status = 0xFF;
volatile uint8_t  cardiac_ble_conn_param_req_status = 0xFF;
/* L2CAP response: 0 = accepted, 1 = rejected, 0xFFFF = none */
volatile uint16_t cardiac_ble_conn_param_result = 0xFFFF;
volatile uint8_t  cardiac_ble_conn_update_status = 0xFF;
volatile uint32_t cardiac_ble_conn_update_count = 0;


/* -------------------------------------------------------------------------- */
/* Private variables                                                          */
/* -------------------------------------------------------------------------- */

static uint8_t cardiac_bdaddr[CARDIAC_BDADDR_SIZE];

static Advertising_Set_Parameters_t cardiac_adv_set_params[1];

/* Advertising stops on connection and must be re-enabled after a
   disconnection, outside the event callback (which runs inside
   hci_user_evt_proc()). */
static volatile uint8_t cardiac_ble_restart_adv = 0;

/* Link optimization sequence after a connection */
typedef enum
{
    CARDIAC_LINK_IDLE = 0,
    CARDIAC_LINK_DATA_LENGTH,
    CARDIAC_LINK_PHY,
    CARDIAC_LINK_MTU,
    CARDIAC_LINK_CONN_PARAM,
    CARDIAC_LINK_READ_PHY,
    CARDIAC_LINK_DONE
} Cardiac_LinkStep;

static volatile uint8_t cardiac_link_step = CARDIAC_LINK_IDLE;
static uint32_t cardiac_link_connect_tick = 0;
static uint32_t cardiac_link_retry_tick = 0;
static uint8_t  cardiac_link_retries = 0;


/* -------------------------------------------------------------------------- */
/* Private function prototypes                                                */
/* -------------------------------------------------------------------------- */

static void Cardiac_BLE_UserEvtRx(void *pData);
static tBleStatus Cardiac_BLE_StackInit(void);
static tBleStatus Cardiac_BLE_StartAdvertising(void);
static void Cardiac_BLE_WaitBoot(void);
static void Cardiac_BLE_ConfigureLinkDefaults(void);
static void Cardiac_BLE_LinkProcess(void);


/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void Cardiac_BLE_Init(void)
{
    tBleStatus ret;

    cardiac_ble_init_status = 0xFF;
    cardiac_ble_stage = 0;

    /*
     * Initialize HCI transport.
     *
     * This eventually uses:
     *
     * STM32U5
     *   -> HCI layer
     *   -> USART2
     *   -> GPDMA RX
     *   -> STM32WB05N
     */
    hci_init(Cardiac_BLE_UserEvtRx, NULL);

    /*
     * hci_init() pulses NRST. Wait until the STM32WB05N reports
     * ACI_BLUE_INITIALIZED, i.e. its UART is ready for commands.
     */
    Cardiac_BLE_WaitBoot();
    cardiac_ble_stage = 1;


    /*
     * Software reset of STM32WB05N.
     */
    ret = hci_reset();
    cardiac_ble_version_status =
        hci_read_local_version_information(
            (uint8_t *)&cardiac_ble_hci_version,
            (uint16_t *)&cardiac_ble_hci_revision,
            (uint8_t *)&cardiac_ble_lmp_version,
            (uint16_t *)&cardiac_ble_manufacturer,
            (uint16_t *)&cardiac_ble_lmp_subversion);
    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_ble_init_status = ret;
        return;
    }

    cardiac_ble_stage = 2;


    /*
     * Same stabilization delay used by ST SensorDemo.
     * Runs in the BLE thread: sleep instead of busy-waiting.
     */
    tx_thread_sleep(CARDIAC_BLE_STABILIZATION_TICKS);


    /*
     * Initialize BLE stack:
     *
     * Address
     * TX power
     * GATT
     * GAP
     * Peripheral role
     * Device name
     */
    ret = Cardiac_BLE_StackInit();

    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_ble_init_status = ret;
        return;
    }


    /*
     * Configure and enable advertising.
     */
    ret = Cardiac_BLE_StartAdvertising();

    if (ret != BLE_STATUS_SUCCESS)
    {
        cardiac_ble_init_status = ret;
        return;
    }


    cardiac_ble_init_status = BLE_STATUS_SUCCESS;
}


void Cardiac_BLE_Process(void)
{
    tBleStatus ret;

    /*
     * Process asynchronous HCI/BLE events.
     * Called continuously from the BLE thread.
     */
    hci_user_evt_proc();

    Cardiac_BLE_LinkProcess();

    Cardiac_GATT_Process();

    if ((cardiac_ble_restart_adv != 0U) &&
        (cardiac_ble_init_status == BLE_STATUS_SUCCESS))
    {
        ret = aci_gap_set_advertising_enable(
                ENABLE,
                1,
                cardiac_adv_set_params);

        cardiac_ble_adv_restart_status = ret;

        /* On failure, retry on the next call */
        if (ret == BLE_STATUS_SUCCESS)
        {
            cardiac_ble_restart_adv = 0U;
        }
    }
}


/* -------------------------------------------------------------------------- */
/* Boot synchronization                                                       */
/* -------------------------------------------------------------------------- */

static void Cardiac_BLE_WaitBoot(void)
{
    uint32_t tickstart = HAL_GetTick();

    cardiac_ble_boot_done = 0U;

    while ((cardiac_ble_boot_done == 0U) &&
           ((HAL_GetTick() - tickstart) < CARDIAC_BLE_BOOT_TIMEOUT_MS))
    {
        hci_user_evt_proc();

        /* Runs in the BLE thread: let lower-priority threads run */
        tx_thread_sleep(1);
    }
}


/* -------------------------------------------------------------------------- */
/* BLE stack initialization                                                   */
/* -------------------------------------------------------------------------- */

static tBleStatus Cardiac_BLE_StackInit(void)
{
    tBleStatus ret;

    uint8_t bdaddr_len_out = 0;

    /*
     * ST SensorDemo reads the static random address
     * stored in WB05 NVM at offset 0x80.
     */
    uint8_t config_data_stored_static_random_address = 0x80;

    uint16_t service_handle = 0;
    uint16_t dev_name_char_handle = 0;
    uint16_t appearance_char_handle = 0;
    uint16_t periph_pref_conn_param_char_handle = 0;

    static const uint8_t device_name[] =
    {
        'C','a','r','d','i','a','c',
        '_',
        'S','e','n','s','o','r'
    };


    /* ---------------------------------------------------------------------- */
    /* Read BLE address from STM32WB05N                                       */
    /* ---------------------------------------------------------------------- */

    ret = aci_hal_read_config_data(
            config_data_stored_static_random_address,
            &bdaddr_len_out,
            cardiac_bdaddr);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }


    /*
     * Static random BLE address must have its two MSBs set.
     *
     * Same validation used by ST SensorDemo.
     */
    if ((cardiac_bdaddr[5] & 0xC0U) != 0xC0U)
    {
        return 0xFF;
    }


    /*
     * Configure BLE public address using address read from WB05 NVM.
     */
    ret = aci_hal_write_config_data(
            CONFIG_DATA_PUBADDR_OFFSET,
            bdaddr_len_out,
            cardiac_bdaddr);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 3;


    /* ---------------------------------------------------------------------- */
    /* TX Power                                                               */
    /* ---------------------------------------------------------------------- */

    /*
     * Same setting used by ST SensorDemo.
     */
    ret = aci_hal_set_tx_power_level(0, 25);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }


    /* ---------------------------------------------------------------------- */
    /* GATT                                                                   */
    /* ---------------------------------------------------------------------- */

    ret = aci_gatt_srv_profile_init(
            CARDIAC_GATT_SERVICE_CHANGED_BIT,
            &service_handle);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 4;


    /* ---------------------------------------------------------------------- */
    /* GAP                                                                    */
    /* ---------------------------------------------------------------------- */

    ret = aci_gap_init(
            PRIVACY_DISABLED,
            HCI_ADDR_PUBLIC);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 5;


    /* ---------------------------------------------------------------------- */
    /* GAP Peripheral Role                                                    */
    /* ---------------------------------------------------------------------- */

    ret = aci_gap_profile_init(
            GAP_PERIPHERAL_ROLE,
            PRIVACY_DISABLED,
            &dev_name_char_handle,
            &appearance_char_handle,
            &periph_pref_conn_param_char_handle);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 6;


    /* ---------------------------------------------------------------------- */
    /* Device Name                                                            */
    /* ---------------------------------------------------------------------- */

    ret = aci_gatt_srv_write_handle_value_nwk(
            dev_name_char_handle + 1,
            0,
            sizeof(device_name),
            (uint8_t *)device_name);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 7;


    /* ---------------------------------------------------------------------- */
    /* Cardiac GATT service                                                   */
    /* ---------------------------------------------------------------------- */

    ret = Cardiac_GATT_Init();

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }


    /* ---------------------------------------------------------------------- */
    /* Link defaults (failures are reported but not fatal)                    */
    /* ---------------------------------------------------------------------- */

    Cardiac_BLE_ConfigureLinkDefaults();

    return BLE_STATUS_SUCCESS;
}


/* -------------------------------------------------------------------------- */
/* Link optimization                                                          */
/* -------------------------------------------------------------------------- */

static void Cardiac_BLE_ConfigureLinkDefaults(void)
{
    /*
     * LE events used by this application:
     * bit 0  Connection Complete          bit 6  Data Length Change
     * bit 1  Advertising Report           bit 7  Read Local P-256 Key Complete
     * bit 2  Connection Update Complete   bit 8  Generate DHKey Complete
     * bit 3  Read Remote Features         bit 9  Enhanced Connection Complete
     * bit 4  LTK Request                  bit 11 PHY Update Complete
     * Bit 5 (Remote Connection Parameter Request) stays disabled so that
     * the controller handles the central's requests by itself.
     */
    uint8_t le_event_mask[8] = { 0xDFU, 0x0BU, 0x00U, 0x00U,
                                 0x00U, 0x00U, 0x00U, 0x00U };

    cardiac_ble_event_mask_status = hci_le_set_event_mask(le_event_mask);

    cardiac_ble_default_dle_status = hci_le_write_suggested_default_data_length(
            CARDIAC_LINK_TX_OCTETS,
            CARDIAC_LINK_TX_TIME_US);

    /* ALL_PHYS = 0: the TX / RX preferences below apply */
    cardiac_ble_default_phy_status = hci_le_set_default_phy(
            0x00U,
            CARDIAC_LINK_PHY_2M,
            CARDIAC_LINK_PHY_2M);
}


static void Cardiac_BLE_LinkNext(uint8_t step)
{
    cardiac_link_step = step;
    cardiac_link_retries = 0U;
}


/* Returns 1 when the command should be tried again later */
static uint8_t Cardiac_BLE_LinkRetry(tBleStatus ret, uint32_t now)
{
    if (((ret == BLE_STATUS_BUSY) || (ret == BLE_STATUS_INSUFFICIENT_RESOURCES)) &&
        (cardiac_link_retries < CARDIAC_LINK_MAX_RETRIES))
    {
        cardiac_link_retries++;
        cardiac_link_retry_tick = now;
        return 1U;
    }

    return 0U;
}


/*
 * Runs one step per call, from the BLE thread (never from an event
 * callback, which already runs inside hci_user_evt_proc()).
 */
static void Cardiac_BLE_LinkProcess(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - cardiac_link_connect_tick;
    uint16_t conn = cardiac_ble_conn_handle;
    tBleStatus ret;
    uint8_t tx_phy = 0U;
    uint8_t rx_phy = 0U;

    if ((cardiac_ble_connected == 0U) ||
        (cardiac_link_step == CARDIAC_LINK_IDLE) ||
        (cardiac_link_step == CARDIAC_LINK_DONE))
    {
        return;
    }

    if ((cardiac_link_retries != 0U) &&
        ((now - cardiac_link_retry_tick) < CARDIAC_LINK_RETRY_MS))
    {
        return;
    }

    switch (cardiac_link_step)
    {
    case CARDIAC_LINK_DATA_LENGTH:
        ret = hci_le_set_data_length(conn,
                                     CARDIAC_LINK_TX_OCTETS,
                                     CARDIAC_LINK_TX_TIME_US);
        cardiac_ble_dle_status = ret;
        if (Cardiac_BLE_LinkRetry(ret, now) == 0U)
        {
            Cardiac_BLE_LinkNext(CARDIAC_LINK_PHY);
        }
        break;

    case CARDIAC_LINK_PHY:
        ret = hci_le_set_phy(conn, 0x00U,
                             CARDIAC_LINK_PHY_2M,
                             CARDIAC_LINK_PHY_2M,
                             0x0000U);
        cardiac_ble_phy_status = ret;
        if (Cardiac_BLE_LinkRetry(ret, now) == 0U)
        {
            Cardiac_BLE_LinkNext(CARDIAC_LINK_MTU);
        }
        break;

    case CARDIAC_LINK_MTU:
        if (elapsed < CARDIAC_LINK_MTU_DELAY_MS)
        {
            break;
        }
        /* Only when the phone has not exchanged the MTU by itself */
        if (cardiac_gatt_att_mtu <= CARDIAC_LINK_DEFAULT_ATT_MTU)
        {
            ret = aci_gatt_clt_exchange_config(conn);
            cardiac_ble_mtu_req_status = ret;
            if (Cardiac_BLE_LinkRetry(ret, now) != 0U)
            {
                break;
            }
        }
        Cardiac_BLE_LinkNext(CARDIAC_LINK_CONN_PARAM);
        break;

    case CARDIAC_LINK_CONN_PARAM:
        if (elapsed < CARDIAC_LINK_CONN_PARAM_DELAY_MS)
        {
            break;
        }
        ret = aci_l2cap_connection_parameter_update_req(
                conn,
                CARDIAC_LINK_CONN_INTERVAL_MIN,
                CARDIAC_LINK_CONN_INTERVAL_MAX,
                CARDIAC_LINK_PERIPHERAL_LATENCY,
                CARDIAC_LINK_SUPERVISION_TIMEOUT);
        cardiac_ble_conn_param_req_status = ret;
        if (Cardiac_BLE_LinkRetry(ret, now) == 0U)
        {
            Cardiac_BLE_LinkNext(CARDIAC_LINK_READ_PHY);
        }
        break;

    case CARDIAC_LINK_READ_PHY:
        if (elapsed < CARDIAC_LINK_READ_PHY_DELAY_MS)
        {
            break;
        }
        /* Covers the case where the PHY Update event is not reported */
        if (hci_le_read_phy(conn, &tx_phy, &rx_phy) == BLE_STATUS_SUCCESS)
        {
            cardiac_ble_tx_phy = tx_phy;
            cardiac_ble_rx_phy = rx_phy;
        }
        Cardiac_BLE_LinkNext(CARDIAC_LINK_DONE);
        break;

    default:
        Cardiac_BLE_LinkNext(CARDIAC_LINK_DONE);
        break;
    }
}


/* -------------------------------------------------------------------------- */
/* Advertising                                                                */
/* -------------------------------------------------------------------------- */

static tBleStatus Cardiac_BLE_StartAdvertising(void)
{
    tBleStatus ret;


    /*
     * Legacy advertising packet
     *
     * 02 01 06
     *    -> General Discoverable + BR/EDR not supported
     *
     * 0F 09 "Cardiac_Sensor"
     *    -> Complete Local Name
     */
    uint8_t adv_data[] =
    {
        0x02,
        AD_TYPE_FLAGS,
        FLAG_BIT_LE_GENERAL_DISCOVERABLE_MODE |
        FLAG_BIT_BR_EDR_NOT_SUPPORTED,

        0x0F,
        0x09,
        'C','a','r','d','i','a','c',
        '_',
        'S','e','n','s','o','r'
    };


    /* ---------------------------------------------------------------------- */
    /* Advertising configuration                                              */
    /* ---------------------------------------------------------------------- */

    ret = aci_gap_set_advertising_configuration(
            0,
            GAP_MODE_GENERAL_DISCOVERABLE,
            ADV_DATA_TYPE,
            CARDIAC_ADV_INTERVAL_MIN,
            CARDIAC_ADV_INTERVAL_MAX,
            HCI_ADV_CH_ALL,
            HCI_ADDR_RANDOM_ADDR,
            NULL,
            HCI_INIT_FILTER_ACCEPT_LIST_NONE,
            0,
            HCI_ADV_PHY_LE_1M,
            0,
            HCI_ADV_PHY_LE_1M,
            0,
            0);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 8;


    /* ---------------------------------------------------------------------- */
    /* Advertising data                                                       */
    /* ---------------------------------------------------------------------- */

    ret = aci_gap_set_advertising_data_nwk(
            0,
            ADV_COMPLETE_DATA,
            sizeof(adv_data),
            adv_data);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 9;


    /* ---------------------------------------------------------------------- */
    /* Enable advertising                                                     */
    /* ---------------------------------------------------------------------- */

    cardiac_adv_set_params[0].Advertising_Handle = 0;
    cardiac_adv_set_params[0].Duration = 0;
    cardiac_adv_set_params[0].Max_Extended_Advertising_Events = 0;

    ret = aci_gap_set_advertising_enable(
            ENABLE,
            1,
            cardiac_adv_set_params);

    if (ret != BLE_STATUS_SUCCESS)
    {
        return ret;
    }

    cardiac_ble_stage = 10;


    return BLE_STATUS_SUCCESS;
}


/* -------------------------------------------------------------------------- */
/* HCI event callback                                                         */
/* -------------------------------------------------------------------------- */

static void Cardiac_BLE_UserEvtRx(void *pData)
{
    uint32_t i;

    hci_spi_pckt *hci_pckt =
            (hci_spi_pckt *)pData;


    if ((hci_pckt->type == HCI_EVENT_PKT) ||
        (hci_pckt->type == HCI_EVENT_EXT_PKT))
    {
        void *data;

        hci_event_pckt *event_pckt =
                (hci_event_pckt *)hci_pckt->data;


        if (hci_pckt->type == HCI_EVENT_PKT)
        {
            data = event_pckt->data;
        }
        else
        {
            hci_event_ext_pckt *event_ext_pckt =
                    (hci_event_ext_pckt *)hci_pckt->data;

            data = event_ext_pckt->data;
        }


        /* ------------------------------------------------------------------ */
        /* LE Meta Event                                                      */
        /* ------------------------------------------------------------------ */

        if (event_pckt->evt == EVT_LE_META_EVENT)
        {
            evt_le_meta_event *evt =
                    (evt_le_meta_event *)data;


            for (i = 0;
                 i < (sizeof(hci_le_meta_events_table) /
                      sizeof(hci_le_meta_events_table_type));
                 i++)
            {
                if (evt->subevent ==
                    hci_le_meta_events_table[i].evt_code)
                {
                    hci_le_meta_events_table[i].process(
                            (void *)evt->data);

                    break;
                }
            }
        }


        /* ------------------------------------------------------------------ */
        /* Vendor-specific event                                              */
        /* ------------------------------------------------------------------ */

        else if (event_pckt->evt == EVT_VENDOR)
        {
            evt_blue_aci *blue_evt =
                    (evt_blue_aci *)data;


            for (i = 0;
                 i < (sizeof(hci_vendor_specific_events_table) /
                      sizeof(hci_vendor_specific_events_table_type));
                 i++)
            {
                if (blue_evt->ecode ==
                    hci_vendor_specific_events_table[i].evt_code)
                {
                    hci_vendor_specific_events_table[i].process(
                            (void *)blue_evt->data);

                    break;
                }
            }
        }


        /* ------------------------------------------------------------------ */
        /* Standard HCI event                                                 */
        /* ------------------------------------------------------------------ */

        else
        {
            for (i = 0;
                 i < (sizeof(hci_events_table) /
                      sizeof(hci_events_table_type));
                 i++)
            {
                if (event_pckt->evt ==
                    hci_events_table[i].evt_code)
                {
                    hci_events_table[i].process(data);

                    break;
                }
            }
        }
    }
}


/* -------------------------------------------------------------------------- */
/* HCI / ACI event handlers (override the weak ones in the middleware)        */
/* -------------------------------------------------------------------------- */

void aci_blue_initialized_event(uint8_t Reason_Code)
{
    cardiac_ble_boot_reason = Reason_Code;
    cardiac_ble_boot_done = 1U;
}


static void Cardiac_BLE_SetConnParams(uint16_t Connection_Interval,
                                      uint16_t Peripheral_Latency,
                                      uint16_t Supervision_Timeout)
{
    cardiac_ble_conn_interval_us = (uint32_t)Connection_Interval * 1250U;
    cardiac_ble_conn_latency = Peripheral_Latency;
    cardiac_ble_supervision_timeout_ms = (uint16_t)(Supervision_Timeout * 10U);
}


static void Cardiac_BLE_OnConnected(uint8_t Status,
                                    uint16_t Connection_Handle,
                                    uint16_t Connection_Interval,
                                    uint16_t Peripheral_Latency,
                                    uint16_t Supervision_Timeout)
{
    if (Status == BLE_STATUS_SUCCESS)
    {
        cardiac_ble_conn_handle = Connection_Handle;
        cardiac_ble_connected = 1U;
        cardiac_ble_connection_count++;

        Cardiac_BLE_SetConnParams(Connection_Interval,
                                  Peripheral_Latency,
                                  Supervision_Timeout);

        /* Until the controller reports otherwise */
        cardiac_ble_tx_phy = 1U;
        cardiac_ble_rx_phy = 1U;
        cardiac_ble_max_tx_octets = 27U;
        cardiac_ble_max_rx_octets = 27U;
        cardiac_ble_dle_status = 0xFFU;
        cardiac_ble_phy_status = 0xFFU;
        cardiac_ble_phy_update_status = 0xFFU;
        cardiac_ble_mtu_req_status = 0xFFU;
        cardiac_ble_conn_param_req_status = 0xFFU;
        cardiac_ble_conn_param_result = 0xFFFFU;
        cardiac_ble_conn_update_status = 0xFFU;

        Cardiac_GATT_OnConnected(Connection_Handle);

        /* Start the link optimization sequence (Cardiac_BLE_LinkProcess) */
        cardiac_link_connect_tick = HAL_GetTick();
        Cardiac_BLE_LinkNext(CARDIAC_LINK_DATA_LENGTH);
    }
}


void hci_le_connection_complete_event(uint8_t Status,
                                      uint16_t Connection_Handle,
                                      uint8_t Role,
                                      uint8_t Peer_Address_Type,
                                      uint8_t Peer_Address[6],
                                      uint16_t Connection_Interval,
                                      uint16_t Peripheral_Latency,
                                      uint16_t Supervision_Timeout,
                                      uint8_t Central_Clock_Accuracy)
{
    Cardiac_BLE_OnConnected(Status, Connection_Handle, Connection_Interval,
                            Peripheral_Latency, Supervision_Timeout);
}


void hci_le_enhanced_connection_complete_event(uint8_t Status,
                                               uint16_t Connection_Handle,
                                               uint8_t Role,
                                               uint8_t Peer_Address_Type,
                                               uint8_t Peer_Address[6],
                                               uint8_t Local_Resolvable_Private_Address[6],
                                               uint8_t Peer_Resolvable_Private_Address[6],
                                               uint16_t Connection_Interval,
                                               uint16_t Peripheral_Latency,
                                               uint16_t Supervision_Timeout,
                                               uint8_t Central_Clock_Accuracy)
{
    Cardiac_BLE_OnConnected(Status, Connection_Handle, Connection_Interval,
                            Peripheral_Latency, Supervision_Timeout);
}


void hci_le_enhanced_connection_complete_v2_event(uint8_t Status,
                                                  uint16_t Connection_Handle,
                                                  uint8_t Role,
                                                  uint8_t Peer_Address_Type,
                                                  uint8_t Peer_Address[6],
                                                  uint8_t Local_Resolvable_Private_Address[6],
                                                  uint8_t Peer_Resolvable_Private_Address[6],
                                                  uint16_t Connection_Interval,
                                                  uint16_t Peripheral_Latency,
                                                  uint16_t Supervision_Timeout,
                                                  uint8_t Central_Clock_Accuracy,
                                                  uint8_t Advertising_Handle,
                                                  uint16_t Sync_Handle)
{
    Cardiac_BLE_OnConnected(Status, Connection_Handle, Connection_Interval,
                            Peripheral_Latency, Supervision_Timeout);
}


void hci_disconnection_complete_event(uint8_t Status,
                                      uint16_t Connection_Handle,
                                      uint8_t Reason)
{
    if (Status == BLE_STATUS_SUCCESS)
    {
        cardiac_ble_connected = 0U;
        cardiac_ble_disconnect_reason = Reason;
        cardiac_link_step = CARDIAC_LINK_IDLE;

        Cardiac_GATT_OnDisconnected();

        /* Advertising stopped when the connection was created */
        cardiac_ble_restart_adv = 1U;
    }
}


void hci_le_connection_update_complete_event(uint8_t Status,
                                             uint16_t Connection_Handle,
                                             uint16_t Connection_Interval,
                                             uint16_t Peripheral_Latency,
                                             uint16_t Supervision_Timeout)
{
    cardiac_ble_conn_update_status = Status;

    if (Status == BLE_STATUS_SUCCESS)
    {
        cardiac_ble_conn_update_count++;
        Cardiac_BLE_SetConnParams(Connection_Interval,
                                  Peripheral_Latency,
                                  Supervision_Timeout);
    }
}


void hci_le_data_length_change_event(uint16_t Connection_Handle,
                                     uint16_t MaxTxOctets,
                                     uint16_t MaxTxTime,
                                     uint16_t MaxRxOctets,
                                     uint16_t MaxRxTime)
{
    cardiac_ble_max_tx_octets = MaxTxOctets;
    cardiac_ble_max_rx_octets = MaxRxOctets;
}


void hci_le_phy_update_complete_event(uint8_t Status,
                                      uint16_t Connection_Handle,
                                      uint8_t TX_PHY,
                                      uint8_t RX_PHY)
{
    cardiac_ble_phy_update_status = Status;

    if (Status == BLE_STATUS_SUCCESS)
    {
        cardiac_ble_tx_phy = TX_PHY;
        cardiac_ble_rx_phy = RX_PHY;
    }
}


void aci_l2cap_connection_update_resp_event(uint16_t Connection_Handle,
                                            uint16_t Result)
{
    cardiac_ble_conn_param_result = Result;
}
