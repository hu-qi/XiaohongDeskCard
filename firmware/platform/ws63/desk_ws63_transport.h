#ifndef XIAOHONG_DESK_WS63_TRANSPORT_H
#define XIAOHONG_DESK_WS63_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desk_core.h"
#include "desk_radio_ingress.h"
#include "desk_reply_mailbox.h"

/*
 * Board-level transport wiring for the original Xiaohong WS63 desk card.
 *
 * This module owns the three SDK-facing ingress paths and the reply mailbox
 * that carries results back to the originating worker task:
 *
 *   BLE GATT write  -> desk_ws63_ble_on_write()  -> dispatcher (UI task)
 *   SLE SSAP write  -> desk_ws63_sle_on_write()  -> dispatcher (UI task)
 *   HTTP PUT/GET    -> desk_ws63_http_put/get()  -> dispatcher (UI task)
 *
 * Every path funnels into the single dispatcher queue drained by LvglTask, so
 * LVGL and Desk Core are only ever touched from the UI task.
 *
 * Authorization is per-connection and time-limited:
 *   - BLE/SLE: the first valid write on a bonded link establishes the session;
 *     the token is the connection generation, never a MAC address.
 *   - HTTP: a Bearer token is compared against the value provisioned over BLE.
 * A peer that has not completed the ownership check is never authorized, and
 * authorize_on_ui re-checks the generation when the UI task actually runs.
 */

#define DESK_TRANSPORT_TIMEOUT_MS 5000u
#define DESK_HTTP_TOKEN_LEN 33u

typedef struct {
    desk_reply_mailbox_t replies;
    desk_dispatcher_t *dispatcher;
    desk_core_t *core;

    /* BLE state */
    desk_radio_link_t ble;
    uint32_t ble_generation;

    /* SLE state */
    desk_radio_link_t sle;
    uint32_t sle_generation;

    /* HTTP bearer token; empty until provisioned. */
    char http_token[DESK_HTTP_TOKEN_LEN];
    /* Shared by PUT and GET so their peer tokens can never collide. */
    uint32_t http_generation;

    /* Reply-mailbox mutex, supplied by the platform. NULL = single task. */
    void (*lock_fn)(void *ctx);
    void (*unlock_fn)(void *ctx);
    void *lock_ctx;

    bool ready;
} desk_transport_t;

typedef struct {
    desk_dispatcher_t *dispatcher;
    desk_core_t *core;
    /* Mutex for the reply mailbox. NULL in single-task host tests. */
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *lock_ctx;
} desk_transport_config_t;

bool desk_transport_init(desk_transport_t *transport,
                         const desk_transport_config_t *config);

/* ---- BLE ---- */

/* New connection: allocates a fresh generation token. */
uint32_t desk_transport_ble_open(desk_transport_t *transport);
void desk_transport_ble_close(desk_transport_t *transport, uint32_t generation);
/* Called from the GATT write callback with one BEGIN/CHUNK/COMMIT packet. */
desk_radio_result_t desk_transport_ble_write(desk_transport_t *transport,
                                             uint32_t generation,
                                             const uint8_t *packet,
                                             size_t size, uint32_t now_ms);
/* Copy the pending ACK for this connection, if any. */
bool desk_transport_ble_take_ack(desk_transport_t *transport,
                                 uint32_t generation, uint8_t out[16]);

/* ---- SLE ---- */

uint32_t desk_transport_sle_open(desk_transport_t *transport);
void desk_transport_sle_close(desk_transport_t *transport, uint32_t generation);
desk_radio_result_t desk_transport_sle_write(desk_transport_t *transport,
                                             uint32_t generation,
                                             const uint8_t *packet,
                                             size_t size, uint32_t now_ms);
bool desk_transport_sle_take_ack(desk_transport_t *transport,
                                 uint32_t generation, uint8_t out[16]);

/* ---- HTTP ---- */

/* Provision the Bearer token; called after a BLE/SLE ownership handshake. */
bool desk_transport_set_http_token(desk_transport_t *transport,
                                   const char *token, size_t len);

/* Authorization is checked against the provisioned token. */
bool desk_transport_http_authorized(const desk_transport_t *transport,
                                    const char *bearer, size_t len);

/* Returns 0 on success (request queued), otherwise a negative HTTP-ish code. */
int desk_transport_http_put(desk_transport_t *transport,
                            const char *bearer, size_t bearer_len,
                            const uint8_t *body, size_t body_len,
                            uint32_t *request_id);
int desk_transport_http_get(desk_transport_t *transport,
                            const char *bearer, size_t bearer_len,
                            uint32_t *peer_token);

/* Copy the pending HTTP reply for peer_token; returns false if none. */
bool desk_transport_http_take_reply(desk_transport_t *transport,
                                    uint32_t peer_token,
                                    desk_reply_item_t *out);

/* ---- shared ---- */

/* UI task: drain replies to transport workers. */
void desk_transport_notify_worker(desk_transport_t *transport);

#endif
