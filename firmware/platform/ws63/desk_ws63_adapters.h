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

/*
 * SLE uses the same 128-bit identities as BLE: DeskServiceIds.ets is the one
 * protocol identity, and the Harmony client passes the same three UUIDs to
 * both BleGattLink and SleSsapLink. Byte order is the human-readable UUID
 * string order, matching the vendor's own bt_uuid_t convention
 * (net_ble_prov.c: ble_uuid16_to_bt_uuid stores the high byte first).
 */
#define DESK_SLE_SERVICE_UUID DESK_BLE_SERVICE_UUID
#define DESK_SLE_COMMAND_UUID DESK_BLE_COMMAND_UUID
#define DESK_SLE_ACK_UUID     DESK_BLE_ACK_UUID

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

/*
 * Register the Desk Card GATT service. Called after enable_ble() succeeds, in
 * the same place the vendor builds its provisioning service. Uses its own
 * GATT server id, so the vendor's provisioning service keeps working; the
 * vendor's global write callback must forward unknown handles to
 * desk_ble_on_write(). Returns false when the board binding is not ready.
 */
bool desk_ble_register_service(void);

/*
 * Keep the desk BLE link in step with the vendor's provisioning state.
 *
 * The vendor only calls enable_ble() from BleProvTask, and main.c only starts
 * that task when the device has no Wi-Fi config. On a normally provisioned
 * device the BLE stack would therefore never be enabled and the Desk Card
 * service would not exist.
 *
 * When the station is connected this registers the GAP/GATTS callbacks, enables
 * the stack and — from the enable callback — builds the service and advertises
 * as "XiaohongDesk". When the device drops back into provisioning it tears the
 * desk service down again, because BLE can only be enabled once and the vendor
 * needs the stack (and its own advertised name, which the WeChat mini-program
 * matches on).
 *
 * Cheap and idempotent; call it from the UI task poll.
 */
void desk_ble_poll(void);

/*
 * Hand the stack back to the vendor before it enables BLE for provisioning.
 * The stack can only be enabled once, so a desk link left over from a previous
 * Wi-Fi session must be torn down first. Called by the vendor hook in
 * net_ble_prov.c; desk_ble_poll() rebuilds the link afterwards.
 */
void desk_ble_release(void);

/* ---- worker pumps ---- */

/*
 * Drain pending ACKs onto the live links. Call from each radio worker's own
 * task loop (the GATT/SLE callbacks must not block). Returns the number of
 * ACKs delivered.
 */
int desk_radio_flush_acks(void);

/* ---- SLE (NearLink) ---- */

/*
 * Register the Desk Card SSAP server, service and the command/ACK properties.
 * Called from the SLE stack-enable callback. Returns false on failure, in
 * which case the caller should leave SLE disabled rather than advertise a
 * service that cannot accept writes.
 */
bool desk_sle_register_service(void);

/*
 * Bootstrap SLE: register the announce/connection/SSAP callbacks and call
 * enable_sle(). Asynchronous — the service table is built from the enable
 * callback. Safe to call once from a non-UI task.
 */
void desk_sle_start(void);

/* Return true when the written handle belongs to the desk command property. */
bool desk_sle_owns_handle(uint16_t handle);

/* Feed one SSAP write; returns true if the handle was ours. */
bool desk_sle_on_write(uint16_t conn_id, uint16_t handle, const uint8_t *value,
                       uint16_t len);

void desk_sle_on_connect(uint16_t conn_id);
void desk_sle_on_disconnect(uint16_t conn_id);

/* Send any pending ACK over the live SLE link. Never call from an ISR. */
bool desk_sle_flush_ack(void);

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

/*
 * The HTTP worker blocks until the UI task answers, so the app never gets an
 * empty 200 for a request Desk Core has not processed yet. Supply the platform
 * sleep (osal_msleep) once at startup; without it the handler never blocks,
 * which is what the host tests want.
 */
void desk_http_set_wait_hook(void (*sleep_ms)(uint32_t ms));

#if defined(DESK_ADAPTERS_HOST_TEST)
/* Host-only: pretend a service table was registered, so handle routing and the
 * ACK flush path can be exercised without a radio stack. */
void desk_ble_test_set_handles(uint16_t cmd_handle, uint16_t ack_handle);
void desk_sle_test_set_handles(uint16_t cmd_handle, uint16_t ack_handle);
#endif

#endif
