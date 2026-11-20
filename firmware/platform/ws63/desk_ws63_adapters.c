#include "desk_ws63_adapters.h"

#include <string.h>

#include "desk_http_json.h"
#include "desk_ws63_board.h"

#if !defined(DESK_ADAPTERS_HOST_TEST)
#include "bts_def.h"
#include "bts_gatt_server.h"
#include "bts_gatt_stru.h"
#include "bts_le_gap.h"
#include "errcode.h"
#include "osal_debug.h"
#include "osal_task.h"
#include "settings.h"
#include "net_wifi_config.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "sle_ssap_stru.h"
#endif

/* ---- shared state ---- */

static uint16_t g_conn_id;
static bool g_connected;

/* Handles are discovered at service-registration time. */
static uint16_t g_cmd_handle;
static uint16_t g_ack_handle;
static bool g_registered;

#if !defined(DESK_ADAPTERS_HOST_TEST)
static uint8_t g_server_id;

void desk_ble_set_handles(uint8_t server_id, uint16_t cmd_handle,
                          uint16_t ack_handle)
{
    g_server_id = server_id;
    g_cmd_handle = cmd_handle;
    g_ack_handle = ack_handle;
    g_registered = true;
}
#endif

/* ---- SLE state ---- */

static uint16_t g_sle_conn_id;
static bool g_sle_connected;
static uint16_t g_sle_cmd_handle;
static uint16_t g_sle_ack_handle;
static bool g_sle_registered;

#if !defined(DESK_ADAPTERS_HOST_TEST)
static uint8_t g_sle_server_id;
#endif

static void sle_set_handles(uint16_t cmd_handle, uint16_t ack_handle)
{
    g_sle_cmd_handle = cmd_handle;
    g_sle_ack_handle = ack_handle;
    g_sle_registered = (cmd_handle != 0u);
}

#if defined(DESK_ADAPTERS_HOST_TEST)
void desk_ble_test_set_handles(uint16_t cmd_handle, uint16_t ack_handle)
{
    g_cmd_handle = cmd_handle;
    g_ack_handle = ack_handle;
    g_registered = (cmd_handle != 0u);
}

void desk_sle_test_set_handles(uint16_t cmd_handle, uint16_t ack_handle)
{
    sle_set_handles(cmd_handle, ack_handle);
}
#endif

bool desk_ble_owns_handle(uint16_t handle)
{
    return g_registered && handle != 0u && handle == g_cmd_handle;
}

bool desk_ble_on_write(uint16_t handle, const uint8_t *value, uint16_t len)
{
    if (!desk_ble_owns_handle(handle)) return false;
    desk_transport_t *t = desk_board_transport();
    if (t == NULL || value == NULL || len == 0u) return true;

    const uint32_t now = 0u; /* Adapt to osKernelGetTickCount() on device. */
    (void)desk_transport_ble_write(t, t->ble_generation, value, len, now);
    return true;
}

void desk_ble_on_connect(uint16_t conn_id)
{
    desk_transport_t *t = desk_board_transport();
    if (t == NULL) return;
    g_conn_id = conn_id;
    g_connected = true;
    const uint32_t gen = desk_transport_ble_open(t);
    /* Ownership: the vendor provisioning link is the trust anchor. A peer
     * that reaches this service has already completed the BLE provisioning
     * handshake, so mark the new generation authorized. */
    if (gen != 0u) t->ble.authorized = true;
}

void desk_ble_on_disconnect(uint16_t conn_id)
{
    desk_transport_t *t = desk_board_transport();
    if (t == NULL) return;
    if (g_connected && conn_id == g_conn_id) {
        desk_transport_ble_close(t, t->ble_generation);
        g_connected = false;
    }
}

bool desk_ble_flush_ack(void)
{
    desk_transport_t *t = desk_board_transport();
    if (t == NULL || !g_connected || !g_registered) return false;

    uint8_t ack[DESK_ACK_SIZE];
    if (!desk_transport_ble_take_ack(t, t->ble_generation, ack)) return false;

#if !defined(DESK_ADAPTERS_HOST_TEST)
    gatts_ntf_ind_t param = {0};
    param.attr_handle = g_ack_handle;
    param.value_len = DESK_ACK_SIZE;
    param.value = ack;
    return gatts_notify_indicate(g_server_id, g_conn_id, &param) ==
           ERRCODE_BT_SUCCESS;
#else
    return true;
#endif
}

#if !defined(DESK_ADAPTERS_HOST_TEST)
/*
 * The vendor defines this locally because the SDK headers do not export it
 * (net_ble_prov.c:64). Same value as the SDK samples.
 */
#define DESK_BLE_ADV_HANDLE 0x01u

/* Set once enable_ble() has been asked to start, so the UI poll does not call
 * it again while the stack is still coming up. */
static bool g_ble_starting;
/* Set once the desk link owns the BLE stack (service registered + advertising). */
static bool g_ble_owned;
/* Set from the stack's disable callback, so release() can wait for it. */
static volatile bool g_ble_disabled;

static void desk_ble_set_local_name(void)
{
    static const char name[] = "XiaohongDesk";
    (void)gap_ble_set_local_name((const uint8_t *)name,
                                 (uint8_t)(sizeof(name) - 1u));
}

static void desk_ble_start_adv(void)
{
    static const uint8_t svc_uuid[16] = DESK_BLE_SERVICE_UUID;
    static const char name[] = "XiaohongDesk";

    /* Flags + complete list of 128-bit service UUIDs (0x07). Advertising data
     * carries the UUID little-endian, same as the GATT table. */
    uint8_t adv[3 + 2 + 16];
    adv[0] = 0x02; adv[1] = 0x01; adv[2] = 0x06;
    adv[3] = 0x11; adv[4] = 0x07;
    for (size_t i = 0; i < 16u; i++) adv[5u + i] = svc_uuid[15u - i];

    uint8_t rsp[2 + sizeof(name) - 1u];
    rsp[0] = (uint8_t)(1u + sizeof(name) - 1u);
    rsp[1] = 0x09; /* Complete Local Name */
    memcpy(&rsp[2], name, sizeof(name) - 1u);

    gap_ble_config_adv_data_t cfg = {0};
    cfg.adv_data = adv;
    cfg.adv_length = (uint16_t)sizeof(adv);
    cfg.scan_rsp_data = rsp;
    cfg.scan_rsp_length = (uint16_t)sizeof(rsp);
    (void)gap_ble_set_adv_data(DESK_BLE_ADV_HANDLE, &cfg);

    gap_ble_adv_params_t param = {0};
    param.min_interval = 0x30; /* 30 ms */
    param.max_interval = 0x60; /* 60 ms */
    param.adv_type = GAP_BLE_ADV_CONN_SCAN_UNDIR;
    param.adv_filter_policy = GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    param.channel_map = 0x07;
    param.duration = 0; /* advertise until stopped */
    (void)gap_ble_set_adv_param(DESK_BLE_ADV_HANDLE, &param);
    (void)gap_ble_start_adv(DESK_BLE_ADV_HANDLE);
}

static void desk_ble_gap_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_BT_SUCCESS) {
        osal_printk("[desk ble] enable failed 0x%x\r\n", status);
        g_ble_starting = false;
        return;
    }
    if (!desk_ble_register_service()) {
        osal_printk("[desk ble] service registration failed\r\n");
        g_ble_starting = false;
        return;
    }
    desk_ble_set_local_name();
    desk_ble_start_adv();
    g_ble_owned = true;
    g_ble_starting = false;
    osal_printk("[desk ble] service ready, advertising as XiaohongDesk\r\n");
}

static void desk_ble_gap_conn_state_cbk(uint16_t conn_id, bd_addr_t *addr,
                                        gap_ble_conn_state_t conn_state,
                                        gap_ble_pair_state_t pair_state,
                                        gap_ble_disc_reason_t disc_reason)
{
    (void)addr;
    (void)pair_state;
    (void)disc_reason;
    if (!g_ble_owned) return;
    if (conn_state == GAP_BLE_STATE_CONNECTED) {
        desk_ble_on_connect(conn_id);
    } else {
        desk_ble_on_disconnect(conn_id);
    }
}

static void desk_ble_gatts_write_cbk(uint8_t server_id, uint16_t conn_id,
                                     gatts_req_write_cb_t *write_cb_para,
                                     errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    if (status != ERRCODE_BT_SUCCESS || write_cb_para == NULL) return;
    /* The desk server is the only one installed on this stack, so every write
     * that reaches this callback is ours. */
    (void)desk_ble_on_write(write_cb_para->handle, write_cb_para->value,
                            write_cb_para->length);
}

static void desk_ble_gatts_mtu_cbk(uint8_t server_id, uint16_t conn_id,
                                   uint16_t mtu_size, errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    (void)status;
    osal_printk("[desk ble] mtu %u\r\n", mtu_size);
}

static void desk_ble_gap_disable_cbk(errcode_t status)
{
    (void)status;
    g_ble_disabled = true;
}

/* Install the GAP/GATTS callbacks. The vendor's provisioning path installs its
 * own on top when it starts; whichever ran last wins, so this is only called
 * while the desk link owns the stack. */
static bool desk_ble_register_callbacks(void)
{
    gap_ble_callbacks_t gap_cb = {0};
    gap_cb.ble_enable_cb = desk_ble_gap_enable_cbk;
    gap_cb.ble_disable_cb = desk_ble_gap_disable_cbk;
    gap_cb.conn_state_change_cb = desk_ble_gap_conn_state_cbk;
    if (gap_ble_register_callbacks(&gap_cb) != ERRCODE_BT_SUCCESS) {
        osal_printk("[desk ble] gap callback registration failed\r\n");
        return false;
    }

    gatts_callbacks_t gatts_cb = {0};
    gatts_cb.write_request_cb = desk_ble_gatts_write_cbk;
    gatts_cb.mtu_changed_cb = desk_ble_gatts_mtu_cbk;
    if (gatts_register_callbacks(&gatts_cb) != ERRCODE_BT_SUCCESS) {
        osal_printk("[desk ble] gatts callback registration failed\r\n");
        return false;
    }
    return true;
}

/* A device in provisioning mode must keep the vendor's stack and its own
 * advertised name: the WeChat mini-program matches on it. */
static bool desk_ble_provisioning(void)
{
    return get_wifi_status() != eWifi_St_Connected;
}

static void desk_ble_begin(void)
{
    if (!desk_ble_register_callbacks()) return;

    g_ble_starting = true;
    const errcode_t ret = enable_ble();
    if (ret != ERRCODE_BT_SUCCESS) {
        osal_printk("[desk ble] enable_ble 0x%x\r\n", ret);
        g_ble_starting = false;
        return;
    }
    /* The service table and advertising are built from the enable callback;
     * g_ble_owned turns true there. */
}

/*
 * Hand the stack back to the vendor before it starts provisioning. BLE can
 * only be enabled once, so the desk service has to be torn down first;
 * desk_ble_poll() rebuilds it if the device leaves provisioning again.
 */
void desk_ble_release(void)
{
    (void)gap_ble_stop_adv(DESK_BLE_ADV_HANDLE);

    g_ble_disabled = false;
    const errcode_t ret = disable_ble();
    if (ret != ERRCODE_BT_SUCCESS) {
        osal_printk("[desk ble] disable_ble 0x%x\r\n", ret);
    } else {
        /* The disable callback runs on the stack thread; do not block forever
         * on a stack that is being torn down anyway. */
        for (uint32_t waited = 0u; waited < 1000u && !g_ble_disabled;
             waited += 5u) {
            (void)osal_msleep(5u);
        }
    }

    /* Forget the handles: the next registration gets fresh ones. */
    g_server_id = 0u;
    g_cmd_handle = 0u;
    g_ack_handle = 0u;
    g_registered = false;
    g_connected = false;
    g_ble_owned = false;
    g_ble_starting = false;
}

void desk_ble_poll(void)
{
    if (desk_board_transport() == NULL) return;
    /* Never touch the stack while enable_ble() is still in flight. */
    if (g_ble_starting) return;

    /* While the device is provisioning the vendor owns the stack. It calls
     * desk_ble_release() itself from BleProvTask before enabling it, so there
     * is nothing to do here — and doing it here would block the UI task. */
    if (desk_ble_provisioning()) return;

    if (!g_ble_owned) desk_ble_begin();
}

bool desk_ble_register_service(void)
{
    if (desk_board_transport() == NULL) return false;
    if (g_registered) return true;

    static const uint8_t svc_uuid[16] = DESK_BLE_SERVICE_UUID;
    static const uint8_t cmd_uuid[16] = DESK_BLE_COMMAND_UUID;
    static const uint8_t ack_uuid[16] = DESK_BLE_ACK_UUID;

    bt_uuid_t app_uuid = {0};
    app_uuid.uuid_len = 16u;
    desk_uuid128_le(svc_uuid, app_uuid.uuid);

    uint8_t server_id = 0;
    if (gatts_register_server(&app_uuid, &server_id) != ERRCODE_BT_SUCCESS)
        return false;

    bt_uuid_t service_uuid = {0};
    service_uuid.uuid_len = 16u;
    desk_uuid128_le(svc_uuid, service_uuid.uuid);
    uint16_t service_handle = 0;
    if (gatts_add_service_sync(server_id, &service_uuid, true,
                               &service_handle) != ERRCODE_BT_SUCCESS)
        return false;

    uint8_t init_value[1] = {0};

    gatts_add_chara_info_t cmd = {0};
    gatts_add_character_result_t cmd_result = {0};
    cmd.chara_uuid.uuid_len = 16u;
    desk_uuid128_le(cmd_uuid, cmd.chara_uuid.uuid);
    cmd.properties = GATT_CHARACTER_PROPERTY_BIT_WRITE |
                     GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP;
    cmd.permissions = GATT_ATTRIBUTE_PERMISSION_WRITE;
    cmd.value_len = sizeof(init_value);
    cmd.value = init_value;
    if (gatts_add_characteristic_sync(server_id, service_handle, &cmd,
                                      &cmd_result) != ERRCODE_BT_SUCCESS)
        return false;

    gatts_add_chara_info_t ack = {0};
    gatts_add_character_result_t ack_result = {0};
    ack.chara_uuid.uuid_len = 16u;
    desk_uuid128_le(ack_uuid, ack.chara_uuid.uuid);
    ack.properties = GATT_CHARACTER_PROPERTY_BIT_NOTIFY;
    ack.permissions = GATT_ATTRIBUTE_PERMISSION_READ;
    ack.value_len = sizeof(init_value);
    ack.value = init_value;
    if (gatts_add_characteristic_sync(server_id, service_handle, &ack,
                                      &ack_result) != ERRCODE_BT_SUCCESS)
        return false;

    uint16_t ccc_handle = 0;
    uint8_t ccc_value[2] = {0, 0};
    gatts_add_desc_info_t ccc = {0};
    ccc.desc_uuid.uuid_len = 2;
    ccc.desc_uuid.uuid[0] = 0x29;
    ccc.desc_uuid.uuid[1] = 0x02; /* Client Characteristic Configuration */
    ccc.permissions = GATT_ATTRIBUTE_PERMISSION_READ |
                      GATT_ATTRIBUTE_PERMISSION_WRITE;
    ccc.value_len = sizeof(ccc_value);
    ccc.value = ccc_value;
    if (gatts_add_descriptor_sync(server_id, service_handle, &ccc,
                                  &ccc_handle) != ERRCODE_BT_SUCCESS)
        return false;

    if (gatts_start_service(server_id, service_handle) != ERRCODE_BT_SUCCESS)
        return false;

    desk_ble_set_handles(server_id, cmd_result.value_handle,
                         ack_result.value_handle);
    return true;
}
#else
bool desk_ble_register_service(void)
{
    return false;
}

void desk_ble_poll(void)
{
}

void desk_ble_release(void)
{
}
#endif

/* ---- worker pumps ---- */

int desk_radio_flush_acks(void)
{
    int sent = 0;
    while (desk_ble_flush_ack()) sent++;
    while (desk_sle_flush_ack()) sent++;
    return sent;
}

/* ---- SLE (NearLink) ---- */

bool desk_sle_owns_handle(uint16_t handle)
{
    return g_sle_registered && handle != 0u && handle == g_sle_cmd_handle;
}

/*
 * BLE stores a 128-bit UUID little-endian, so the bytes on the wire are the
 * reverse of the human-readable string. The Desk header macros are written in
 * string order; reverse them here.
 *
 * This is not cosmetic. Publishing the string order produces a different UUID,
 * and the Harmony client then fails to find the characteristic even though the
 * service is listed: the desk command writes are silently dropped.
 *
 * SLE is the opposite (SDK sample sle_speed_server.c stores string order), so
 * this helper is deliberately BLE-only.
 */
void desk_uuid128_le(const uint8_t src[16], uint8_t out[16])
{
    for (size_t i = 0; i < 16u; i++) out[i] = src[15u - i];
}

bool desk_sle_on_write(uint16_t conn_id, uint16_t handle, const uint8_t *value,
                       uint16_t len)
{
    if (!desk_sle_owns_handle(handle)) return false;
    desk_transport_t *t = desk_board_transport();
    if (t == NULL || value == NULL || len == 0u) return true;
    (void)conn_id;
    const uint32_t now = 0u; /* Adapt to osKernelGetTickCount() on device. */
    (void)desk_transport_sle_write(t, t->sle_generation, value, len, now);
    return true;
}

void desk_sle_on_connect(uint16_t conn_id)
{
    desk_transport_t *t = desk_board_transport();
    if (t == NULL) return;
    g_sle_conn_id = conn_id;
    g_sle_connected = true;
    const uint32_t gen = desk_transport_sle_open(t);
    /* SLE pairing/encryption is the trust anchor, exactly as with BLE. */
    if (gen != 0u) t->sle.authorized = true;
}

void desk_sle_on_disconnect(uint16_t conn_id)
{
    desk_transport_t *t = desk_board_transport();
    if (t == NULL) return;
    if (g_sle_connected && conn_id == g_sle_conn_id) {
        desk_transport_sle_close(t, t->sle_generation);
        g_sle_connected = false;
    }
}

bool desk_sle_flush_ack(void)
{
    desk_transport_t *t = desk_board_transport();
    if (t == NULL || !g_sle_connected || !g_sle_registered) return false;

    uint8_t ack[DESK_ACK_SIZE];
    if (!desk_transport_sle_take_ack(t, t->sle_generation, ack)) return false;

#if !defined(DESK_ADAPTERS_HOST_TEST)
    ssaps_ntf_ind_t param = {0};
    param.handle = g_sle_ack_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value_len = DESK_ACK_SIZE;
    param.value = ack;
    return ssaps_notify_indicate(g_sle_server_id, g_sle_conn_id, &param) ==
           ERRCODE_SLE_SUCCESS;
#else
    return true;
#endif
}

#if !defined(DESK_ADAPTERS_HOST_TEST)
/* ---- SLE SDK glue: the only part of this file that talks to the SLE stack. */

#define DESK_SLE_ADV_HANDLE 1u
#define DESK_SLE_ADV_INTERVAL_MIN 0xC8u /* 25 ms in 125 us units */
#define DESK_SLE_ADV_INTERVAL_MAX 0xC8u
#define DESK_SLE_CONN_INTERVAL_MIN 0x14u /* 2.5 ms */
#define DESK_SLE_CONN_INTERVAL_MAX 0x14u
#define DESK_SLE_CONN_MAX_LATENCY 0x1F3u
#define DESK_SLE_CONN_TIMEOUT 0x1F4u /* 5 s in 10 ms units */
#define DESK_SLE_ADV_TX_POWER 20

/* AD structure types (SDK sample headers do not export them). */
#define DESK_SLE_AD_TYPE_DISCOVERY_LEVEL 0x01u
#define DESK_SLE_AD_TYPE_UUID16_LIST 0x05u
#define DESK_SLE_AD_TYPE_UUID128_LIST 0x06u
#define DESK_SLE_AD_TYPE_LOCAL_NAME 0x0Bu
#define DESK_SLE_AD_TYPE_TX_POWER 0x0Cu

static void sle_uuid128(const uint8_t bytes[16], sle_uuid_t *out)
{
    memset(out, 0, sizeof(*out));
    out->len = 16u;
    memcpy(out->uuid, bytes, 16u);
}

static errcode_t sle_add_service_and_properties(void)
{
    static const uint8_t svc_uuid[16] = DESK_SLE_SERVICE_UUID;
    static const uint8_t cmd_uuid[16] = DESK_SLE_COMMAND_UUID;
    static const uint8_t ack_uuid[16] = DESK_SLE_ACK_UUID;

    sle_uuid_t app_uuid;
    sle_uuid128(svc_uuid, &app_uuid);
    uint8_t server_id = 0;
    errcode_t ret = ssaps_register_server(&app_uuid, &server_id);
    if (ret != ERRCODE_SLE_SUCCESS) return ret;

    sle_uuid_t service_uuid;
    sle_uuid128(svc_uuid, &service_uuid);
    uint16_t service_handle = 0;
    ret = ssaps_add_service_sync(server_id, &service_uuid, true,
                                 &service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(server_id);
        return ret;
    }

    /* Command: write-only, so a client cannot read another peer's frames. */
    uint8_t cmd_value[1] = {0};
    ssaps_property_info_t cmd = {0};
    sle_uuid128(cmd_uuid, &cmd.uuid);
    cmd.permissions = SSAP_PERMISSION_WRITE;
    cmd.operate_indication = SSAP_OPERATE_INDICATION_BIT_WRITE;
    cmd.value_len = sizeof(cmd_value);
    cmd.value = cmd_value;
    uint16_t cmd_handle = 0;
    ret = ssaps_add_property_sync(server_id, service_handle, &cmd, &cmd_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(server_id);
        return ret;
    }

    /* ACK: notify-only. The Harmony client subscribes to propertyChange. */
    uint8_t ack_value[DESK_ACK_SIZE] = {0};
    ssaps_property_info_t ack = {0};
    sle_uuid128(ack_uuid, &ack.uuid);
    ack.permissions = SSAP_PERMISSION_READ;
    ack.operate_indication = SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    ack.value_len = sizeof(ack_value);
    ack.value = ack_value;
    uint16_t ack_handle = 0;
    ret = ssaps_add_property_sync(server_id, service_handle, &ack, &ack_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(server_id);
        return ret;
    }

    /* Client Characteristic Configuration, so the peer can enable notify. */
    uint8_t ccc_value[2] = {0, 0};
    ssaps_desc_info_t ccc = {0};
    memset(&ccc.uuid, 0, sizeof(ccc.uuid));
    ccc.uuid.len = 2u;
    ccc.uuid.uuid[0] = 0x29;
    ccc.uuid.uuid[1] = 0x02;
    ccc.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    ccc.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
                             SSAP_OPERATE_INDICATION_BIT_WRITE;
    ccc.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    ccc.value_len = sizeof(ccc_value);
    ccc.value = ccc_value;
    ret = ssaps_add_descriptor_sync(server_id, service_handle, ack_handle, &ccc);
    if (ret != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(server_id);
        return ret;
    }

    ret = ssaps_start_service(server_id, service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(server_id);
        return ret;
    }

    /* MTU large enough for a 384-byte wire frame plus SSAP overhead. */
    ssap_exchange_info_t info = {0};
    info.mtu_size = 512u;
    info.version = 1u;
    (void)ssaps_set_info(server_id, &info);

    g_sle_server_id = server_id;
    sle_set_handles(cmd_handle, ack_handle);
    return ERRCODE_SLE_SUCCESS;
}

bool desk_sle_register_service(void)
{
    if (desk_board_transport() == NULL) return false;
    if (g_sle_registered) return true;
    return sle_add_service_and_properties() == ERRCODE_SLE_SUCCESS;
}

/*
 * SLE advertising structures are [type][length][value], the reverse of BLE's
 * [length][type][value]. Getting this wrong makes the stack drop the whole
 * block: the device never appears in a scan, even though announce starts
 * successfully and no error is reported.
 *
 * Verified against the SDK sample (sle_speed_server_adv.c:38).
 */
static void desk_sle_announce_start(void)
{
    static const uint8_t svc_uuid[16] = DESK_SLE_SERVICE_UUID;
    static const char name[] = "XiaohongDesk";

    /* announce data: discovery level + complete 128-bit service UUID list, so
     * the Harmony client can find the device by service as well as by name. */
    uint8_t adv[3 + 2 + 16];
    size_t n = 0;
    adv[n++] = DESK_SLE_AD_TYPE_DISCOVERY_LEVEL;
    adv[n++] = 1u;
    adv[n++] = 2u; /* SLE_ANNOUNCE_LEVEL_NORMAL */
    adv[n++] = DESK_SLE_AD_TYPE_UUID128_LIST;
    adv[n++] = 16u;
    /* AD data carries multi-byte values little-endian, same as the 16-bit UUID
     * in the SDK sample (0x0B, 0x06 == 0x060B). */
    desk_uuid128_le(svc_uuid, &adv[n]);
    n += sizeof(svc_uuid);

    /* scan response: local name, same [type][length][value] layout. */
    uint8_t rsp[2 + sizeof(name) - 1u];
    rsp[0] = DESK_SLE_AD_TYPE_LOCAL_NAME;
    rsp[1] = (uint8_t)(sizeof(name) - 1u);
    memcpy(&rsp[2], name, sizeof(name) - 1u);

    sle_announce_data_t data = {0};
    data.announce_data = adv;
    data.announce_data_len = (uint16_t)n;
    data.seek_rsp_data = rsp;
    data.seek_rsp_data_len = (uint16_t)sizeof(rsp);
    (void)sle_set_announce_data(DESK_SLE_ADV_HANDLE, &data);

    sle_announce_param_t param = {0};
    param.announce_handle = DESK_SLE_ADV_HANDLE;
    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = 0x07u;
    param.announce_interval_min = DESK_SLE_ADV_INTERVAL_MIN;
    param.announce_interval_max = DESK_SLE_ADV_INTERVAL_MAX;
    param.announce_tx_power = DESK_SLE_ADV_TX_POWER;
    param.conn_interval_min = DESK_SLE_CONN_INTERVAL_MIN;
    param.conn_interval_max = DESK_SLE_CONN_INTERVAL_MAX;
    param.conn_max_latency = DESK_SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = DESK_SLE_CONN_TIMEOUT;
    const errcode_t pr = sle_set_announce_param(DESK_SLE_ADV_HANDLE, &param);
    const errcode_t ar = sle_start_announce(DESK_SLE_ADV_HANDLE);
    osal_printk("[desk sle] announce param 0x%x start 0x%x\r\n", pr, ar);
}

static void sle_enable_cbk(errcode_t status)
{
    osal_printk("[desk sle] enable cbk status 0x%x\r\n", status);
    if (status != ERRCODE_SLE_SUCCESS) return;

    if (!desk_sle_register_service()) {
        osal_printk("[desk sle] service registration failed\r\n");
        return;
    }
    osal_printk("[desk sle] service ready, announcing as XiaohongDesk\r\n");
    desk_sle_announce_start();
}

static void sle_connect_state_changed_cbk(uint16_t conn_id,
                                          const sle_addr_t *addr,
                                          sle_acb_state_t conn_state,
                                          sle_pair_state_t pair_state,
                                          sle_disc_reason_t disc_reason)
{
    (void)addr;
    (void)pair_state;
    (void)disc_reason;
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        desk_sle_on_connect(conn_id);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        desk_sle_on_disconnect(conn_id);
        desk_sle_announce_start();
    }
}

static void sle_write_request_cbk(uint8_t server_id, uint16_t conn_id,
                                  ssaps_req_write_cb_t *write_cb_para,
                                  errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS || write_cb_para == NULL) return;
    if (server_id != g_sle_server_id) return;

    const bool ours = desk_sle_on_write(conn_id, write_cb_para->handle,
                                        write_cb_para->value,
                                        write_cb_para->length);
    if (write_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_cb_para->request_id;
        rsp.status = ours ? (uint8_t)ERRCODE_SLE_SUCCESS
                          : (uint8_t)ERRCODE_SLE_FAIL;
        rsp.value_len = 0;
        rsp.value = NULL;
        (void)ssaps_send_response(server_id, conn_id, &rsp);
    }
}

static void sle_read_request_cbk(uint8_t server_id, uint16_t conn_id,
                                 ssaps_req_read_cb_t *read_cb_para,
                                 errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS || read_cb_para == NULL) return;
    ssaps_send_rsp_t rsp = {0};
    rsp.request_id = read_cb_para->request_id;
    rsp.status = (uint8_t)ERRCODE_SLE_SUCCESS;
    rsp.value_len = 0;
    rsp.value = NULL;
    (void)ssaps_send_response(server_id, conn_id, &rsp);
}

static void sle_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id,
                                ssap_exchange_info_t *mtu_size,
                                errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    (void)mtu_size;
    (void)status;
}

/* Adapt the platform sleep to the hook signature the HTTP wait loop uses. */
static void desk_sleep_ms(uint32_t ms)
{
    (void)osal_msleep(ms);
}

void desk_sle_start(void)
{
    desk_http_set_wait_hook(desk_sleep_ms);

    sle_announce_seek_callbacks_t seek_cbks = {0};
    seek_cbks.sle_enable_cb = sle_enable_cbk;
    (void)sle_announce_seek_register_callbacks(&seek_cbks);

    sle_connection_callbacks_t conn_cbks = {0};
    conn_cbks.connect_state_changed_cb = sle_connect_state_changed_cbk;
    (void)sle_connection_register_callbacks(&conn_cbks);

    ssaps_callbacks_t ssaps_cbks = {0};
    ssaps_cbks.write_request_cb = sle_write_request_cbk;
    ssaps_cbks.read_request_cb = sle_read_request_cbk;
    ssaps_cbks.mtu_changed_cb = sle_mtu_changed_cbk;
    (void)ssaps_register_callbacks(&ssaps_cbks);

    const errcode_t ret = enable_sle();
    osal_printk("[desk sle] enable_sle 0x%x\r\n", ret);
}
#else
bool desk_sle_register_service(void)
{
    return false;
}

void desk_sle_start(void)
{
}
#endif

/* ---- HTTP ---- */

static bool header_value(const char *headers, const char *name, size_t name_len,
                         const char **out, size_t *out_len)
{
    if (headers == NULL) return false;
    const char *p = headers;
    while (*p != '\0') {
        /* Case-insensitive compare of the field name at a line start. */
        size_t i = 0;
        while (i < name_len) {
            char a = p[i];
            char b = name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            i++;
        }
        if (i == name_len && p[name_len] == ':') {
            const char *v = p + name_len + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char *e = v;
            while (*e != '\0' && *e != '\r' && *e != '\n') e++;
            *out = v;
            *out_len = (size_t)(e - v);
            return true;
        }
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return false;
}

static bool path_is(const char *request_line, const char *method,
                    const char *path)
{
    const size_t mlen = strlen(method);
    if (strncmp(request_line, method, mlen) != 0) return false;
    if (request_line[mlen] != ' ') return false;
    const char *p = request_line + mlen + 1;
    const size_t plen = strlen(path);
    if (strncmp(p, path, plen) != 0) return false;
    /* Require a delimiter so /api/v1/desk/contentX does not match. */
    return p[plen] == ' ' || p[plen] == '?' || p[plen] == '\0';
}

/*
 * The UI task (LvglTask) is the only consumer of the dispatcher queue and it
 * polls every few milliseconds. The HTTP worker therefore waits here instead
 * of returning an empty 200 that the app would read as "no content".
 *
 * sleep_ms is supplied by the platform (osal_msleep on device, NULL in host
 * tests where the UI task has already run).
 */
static void (*g_sleep_ms)(uint32_t ms);

void desk_http_set_wait_hook(void (*sleep_ms)(uint32_t ms))
{
    g_sleep_ms = sleep_ms;
}

#define DESK_HTTP_WAIT_STEP_MS 5u
#define DESK_HTTP_WAIT_MAX_MS 1000u

static bool take_reply_wait(desk_transport_t *t, uint32_t peer,
                            desk_reply_item_t *item)
{
    if (desk_transport_http_take_reply(t, peer, item)) return true;
    if (g_sleep_ms == NULL) return false;
    for (uint32_t waited = 0; waited < DESK_HTTP_WAIT_MAX_MS;
         waited += DESK_HTTP_WAIT_STEP_MS) {
        g_sleep_ms(DESK_HTTP_WAIT_STEP_MS);
        if (desk_transport_http_take_reply(t, peer, item)) return true;
    }
    return false;
}

desk_http_result_t desk_http_handle_request(const char *request_line,
                                            const char *headers,
                                            const uint8_t *body,
                                            size_t body_len,
                                            char *out, size_t out_cap,
                                            size_t *out_len)
{
    if (request_line == NULL || out == NULL || out_len == NULL) return
        DESK_HTTP_NOT_HANDLED;
    desk_transport_t *t = desk_board_transport();
    if (t == NULL) return DESK_HTTP_NOT_HANDLED;

    const bool is_put = path_is(request_line, "PUT", "/api/v1/desk/content");
    const bool is_get = path_is(request_line, "GET", "/api/v1/desk/status");
    if (!is_put && !is_get) return DESK_HTTP_NOT_HANDLED;

    /* Authorization: Bearer <token> */
    const char *auth = NULL;
    size_t auth_len = 0;
    if (!header_value(headers, "Authorization", 13u, &auth, &auth_len) ||
        auth_len <= 7u || strncmp(auth, "Bearer ", 7u) != 0)
        return DESK_HTTP_HANDLED_UNAUTH;
    const char *bearer = auth + 7;
    const size_t bearer_len = auth_len - 7u;

    if (is_put) {
        uint32_t request_id = 0;
        const int rc = desk_transport_http_put(t, bearer, bearer_len, body,
                                               body_len, &request_id);
        if (rc == -401) return DESK_HTTP_HANDLED_UNAUTH;
        if (rc == -429) return DESK_HTTP_HANDLED_BUSY;
        if (rc != 0) return DESK_HTTP_HANDLED_BAD;

        /* The UI task processes the queued write on its next poll; wait for the
         * ACK so the response body reflects the real Desk Core result. */
        desk_reply_item_t item;
        if (!take_reply_wait(t, request_id, &item)) {
            *out_len = 0;
            return DESK_HTTP_HANDLED_OK; /* accepted, UI task did not answer. */
        }
        desk_ack_t ack;
        if (item.kind == DESK_REPLY_ACK &&
            desk_wire_decode_ack(item.payload.ack, DESK_ACK_SIZE, &ack) ==
                DESK_WIRE_OK) {
            desk_http_state_t state = {0};
            state.revision = ack.revision;
            *out_len = desk_http_encode_reply(ack.result, &state, out, out_cap);
            return DESK_HTTP_HANDLED_OK;
        }
        *out_len = 0;
        return DESK_HTTP_HANDLED_OK;
    }

    /* GET status */
    uint32_t peer = 0;
    if (desk_transport_http_get(t, bearer, bearer_len, &peer) != 0)
        return DESK_HTTP_HANDLED_UNAUTH;

    desk_reply_item_t item;
    if (!take_reply_wait(t, peer, &item) || item.kind != DESK_REPLY_STATUS) {
        *out_len = 0;
        return DESK_HTTP_HANDLED_OK;
    }
    if (item.result != DESK_OK) {
        *out_len = desk_http_encode_reply(item.result, NULL, out, out_cap);
        return DESK_HTTP_HANDLED_OK;
    }
    *out_len = desk_http_encode_status(&item.payload.state, out, out_cap);
    return DESK_HTTP_HANDLED_OK;
}
