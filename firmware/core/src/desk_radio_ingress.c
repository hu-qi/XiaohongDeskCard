#include "desk_radio_ingress.h"

#include <string.h>

bool desk_radio_link_open(desk_radio_link_t *link,
                          desk_dispatcher_t *dispatcher,
                          desk_channel_t channel, uint32_t peer_token)
{
    if (link == NULL || dispatcher == NULL || dispatcher->core == NULL ||
        (channel != DESK_CHANNEL_BLE && channel != DESK_CHANNEL_SLE) ||
        peer_token == 0u) return false;

    /* Never reinitialize an active connection; that would erase a pending
     * transaction without allowing the transport to reconcile its outcome. */
    if (link->connected) return false;
    memset(link, 0, sizeof(*link));
    link->dispatcher = dispatcher;
    link->channel = channel;
    link->peer_token = peer_token;
    link->connected = true;
    return true;
}

void desk_radio_link_set_authorized(desk_radio_link_t *link, bool authorized)
{
    if (link == NULL || !link->connected) return;
    if (link->authorized != authorized) {
        /* A fragmented unauthenticated (or newly revoked) command cannot
         * continue after the security state changes. */
        desk_packet_reset(&link->packet);
    }
    link->authorized = authorized;
}

desk_radio_result_t desk_radio_link_receive(desk_radio_link_t *link,
                                             const uint8_t *packet,
                                             size_t packet_size,
                                             uint32_t now_ms,
                                             uint32_t timeout_ms)
{
    if (link == NULL || packet == NULL || packet_size == 0u)
        return DESK_RADIO_INVALID_ARGUMENT;
    if (!link->connected || link->dispatcher == NULL)
        return DESK_RADIO_NOT_CONNECTED;
    if (!link->authorized) {
        desk_packet_reset(&link->packet);
        return DESK_RADIO_NOT_AUTHORIZED;
    }
    if (link->awaiting_ack) return DESK_RADIO_AWAITING_ACK;

    desk_set_command_t decoded = {0};
    const desk_packet_result_t packet_result =
        desk_packet_feed(&link->packet, packet, packet_size,
                         now_ms, timeout_ms, &decoded);
    if (packet_result == DESK_PACKET_PROGRESS) return DESK_RADIO_PROGRESS;
    if (packet_result != DESK_PACKET_READY) return DESK_RADIO_BAD_PACKET;

    const desk_dispatch_item_t item = {
        link->channel,
        link->peer_token,
        true,  /* authorization snapshot taken at accepted COMMIT */
        decoded,
        false
    };
    const desk_dispatch_status_t status =
        desk_dispatcher_submit(link->dispatcher, &item);
    if (status == DESK_DISPATCH_BUSY) return DESK_RADIO_QUEUE_FULL;
    if (status != DESK_DISPATCH_QUEUED) return DESK_RADIO_INVALID_ARGUMENT;

    link->pending_request_id = decoded.request_id;
    link->awaiting_ack = true;
    return DESK_RADIO_QUEUED;
}

bool desk_radio_link_ack_complete(desk_radio_link_t *link, uint32_t peer_token,
                                   const uint8_t *ack, size_t ack_size)
{
    if (link == NULL || !link->connected || !link->awaiting_ack ||
        peer_token == 0u || peer_token != link->peer_token)
        return false;
    desk_ack_t result = {0};
    if (desk_wire_decode_ack(ack, ack_size, &result) != DESK_WIRE_OK ||
        result.request_id != link->pending_request_id)
        return false;

    link->awaiting_ack = false;
    link->pending_request_id = 0u;
    return true;
}

void desk_radio_link_close(desk_radio_link_t *link)
{
    if (link == NULL) return;
    memset(link, 0, sizeof(*link));
}
