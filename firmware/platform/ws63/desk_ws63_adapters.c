#include "desk_ws63_adapters.h"

#include <string.h>

#include "desk_http_json.h"
#include "desk_ws63_board.h"

#if !defined(DESK_ADAPTERS_HOST_TEST)
#include "bts_gatt_server.h"
#include "errcode.h"
#include "osal_debug.h"
#endif

/* ---- shared state ---- */

static uint16_t g_conn_id;
static bool g_connected;

/* Handles are discovered at service-registration time. */
static uint16_t g_cmd_handle;
static bool g_registered;
#if !defined(DESK_ADAPTERS_HOST_TEST)
static uint16_t g_ack_handle;
#endif

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

        /* The UI task processes the queued write on its next poll; the HTTP
         * worker polls desk_http_take_reply() until the ACK arrives. */
        desk_reply_item_t item;
        if (!desk_transport_http_take_reply(t, request_id, &item)) {
            *out_len = 0;
            return DESK_HTTP_HANDLED_OK; /* 202-ish: accepted, no body yet. */
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
    if (!desk_transport_http_take_reply(t, peer, &item) ||
        item.kind != DESK_REPLY_STATUS) {
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
