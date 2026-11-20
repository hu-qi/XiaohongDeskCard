#ifndef XIAOHONG_DESK_RADIO_INGRESS_H
#define XIAOHONG_DESK_RADIO_INGRESS_H

#include "desk_packet.h"
#include "desk_dispatcher.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Transport-independent ingress for one BLE GATT or SLE SSAP connection.
 *
 * One instance belongs to ONE connection worker. All functions on this
 * instance must be serialized by that worker, including ack_complete().
 * The UI task must enqueue ACK bytes for the worker instead of calling
 * ack_complete() concurrently.
 *
 * This module is not a radio driver and does not itself authenticate peers.
 * Set authorized=true ONLY after the SDK has verified pairing + Desk
 * ownership. A radio address or advertised name is NOT proof of ownership.
 */
typedef enum {
    DESK_RADIO_PROGRESS = 0,
    DESK_RADIO_QUEUED,
    DESK_RADIO_INVALID_ARGUMENT,
    DESK_RADIO_NOT_CONNECTED,
    DESK_RADIO_NOT_AUTHORIZED,
    DESK_RADIO_BAD_PACKET,
    DESK_RADIO_AWAITING_ACK,
    DESK_RADIO_QUEUE_FULL
} desk_radio_result_t;

typedef struct {
    desk_packet_session_t packet;
    desk_dispatcher_t *dispatcher;
    desk_channel_t channel;
    uint32_t peer_token;
    uint32_t pending_request_id;
    bool connected;
    bool authorized;
    bool awaiting_ack;
} desk_radio_link_t;

/*
 * peer_token must be nonzero and UNIQUE for the lifetime of each physical
 * connection, including after reconnect. A token is an opaque session
 * generation identifier, NOT a persistent MAC/identity credential.
 */
bool desk_radio_link_open(desk_radio_link_t *link,
                          desk_dispatcher_t *dispatcher,
                          desk_channel_t channel, uint32_t peer_token);

/* Called only after the transport's trusted ownership/security check. */
void desk_radio_link_set_authorized(desk_radio_link_t *link, bool authorized);

/*
 * Called in the connection worker context, NOT inside an ISR.
 * A valid complete command is queued for LVGL owner task processing.
 * Radio write success never means the command has been applied.
 */
desk_radio_result_t desk_radio_link_receive(desk_radio_link_t *link,
                                             const uint8_t *packet,
                                             size_t packet_size,
                                             uint32_t now_ms,
                                             uint32_t timeout_ms);

/*
 * The UI task's dispatcher callback sends ACK to a worker queue. AFTER the
 * worker has dealt with the ACK, call this with the same active peer token.
 * Returns false for stale/wrong-peer/invalid ACK; does not clear pending.
 */
bool desk_radio_link_ack_complete(desk_radio_link_t *link, uint32_t peer_token,
                                   const uint8_t *ack, size_t ack_size);

void desk_radio_link_close(desk_radio_link_t *link);

#endif
