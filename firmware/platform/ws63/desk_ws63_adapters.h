#ifndef XIAOHONG_DESK_WS63_ADAPTERS_H
#define XIAOHONG_DESK_WS63_ADAPTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SDK-facing adapters. These are the only files that know about the WS63
 * GATT and lwIP APIs; everything they call is portable C.
 *
 * Registration is deliberately explicit and one-shot:
 *   desk_ble_register_service()  after the vendor GATT server exists
 *   desk_http_handle_request()   from the vendor HTTP request dispatcher
 *
 * Both return false when the board binding is not initialised, so a firmware
 * built without the desk hook fails closed instead of half-working.
 */

/* BLE service identity, matching apps/harmony/.../DeskServiceIds.ets. */
#define DESK_BLE_SERVICE_UUID \
    { 0x79, 0xb4, 0x68, 0xbd, 0x30, 0x6c, 0x58, 0xa8, \
      0x94, 0x15, 0xc9, 0xe9, 0xc9, 0x93, 0xbd, 0xfa }
#define DESK_BLE_COMMAND_UUID \
    { 0x44, 0xb4, 0xcc, 0x5b, 0x14, 0xfe, 0x54, 0xc2, \
      0xbc, 0x82, 0xe9, 0x80, 0x80, 0x2e, 0x20, 0x52 }
#define DESK_BLE_ACK_UUID \
    { 0xe7, 0x40, 0xd8, 0x24, 0xfa, 0x27, 0x58, 0xc3, \
      0x8b, 0xb9, 0x08, 0x5f, 0x74, 0xa3, 0x18, 0x69 }

/* Return true when the written handle belongs to the desk command char. */
bool desk_ble_owns_handle(uint16_t handle);

/* Feed one ATT write value; returns true if the handle was ours. */
bool desk_ble_on_write(uint16_t handle, const uint8_t *value, uint16_t len);

/* Call when a GATT link comes up / goes down. */
void desk_ble_on_connect(uint16_t conn_id);
void desk_ble_on_disconnect(uint16_t conn_id);

/*
 * Send any pending ACK for the live connection. Call from the GATT worker
 * (never an ISR). Returns true when a notification was queued.
 */
bool desk_ble_flush_ack(void);

/* ---- HTTP ---- */

/* Result codes the vendor HTTP dispatcher maps onto status lines. */
typedef enum {
    DESK_HTTP_NOT_HANDLED = 0,
    DESK_HTTP_HANDLED_OK = 200,
    DESK_HTTP_HANDLED_BAD = 400,
    DESK_HTTP_HANDLED_UNAUTH = 401,
    DESK_HTTP_HANDLED_BUSY = 429
} desk_http_result_t;

/*
 * Handle /api/v1/desk/content (PUT) and /api/v1/desk/status (GET).
 * request_line is the raw first line ("PUT /api/v1/desk/content HTTP/1.1").
 * headers is the raw header block (used for Authorization and Content-Length).
 * body/body_len is the request body for PUT.
 *
 * On HANDLED_OK, out/out_cap receives the JSON response body.
 */
desk_http_result_t desk_http_handle_request(const char *request_line,
                                            const char *headers,
                                            const uint8_t *body,
                                            size_t body_len,
                                            char *out, size_t out_cap,
                                            size_t *out_len);

#endif
