#include "desk_wifi_ingress.h"

desk_wifi_result_t desk_wifi_submit_put(desk_dispatcher_t *dispatcher,
                                        uint32_t peer_token, bool authorized,
                                        const uint8_t *body, size_t body_size,
                                        uint32_t *request_id)
{
    if (dispatcher == NULL || dispatcher->core == NULL || peer_token == 0u ||
        body == NULL || body_size == 0u || request_id == NULL)
        return DESK_WIFI_BAD_REQUEST;
    if (!authorized) return DESK_WIFI_UNAUTHORIZED;

    desk_set_command_t command = {0};
    if (!desk_http_decode_set(body, body_size, &command))
        return DESK_WIFI_BAD_REQUEST;

    const desk_dispatch_item_t item = {
        DESK_CHANNEL_WIFI, peer_token, true, command
    };
    const desk_dispatch_status_t result =
        desk_dispatcher_submit(dispatcher, &item);
    if (result == DESK_DISPATCH_BUSY) return DESK_WIFI_BUSY;
    if (result != DESK_DISPATCH_QUEUED) return DESK_WIFI_BAD_REQUEST;
    *request_id = command.request_id;
    return DESK_WIFI_QUEUED;
}

desk_wifi_result_t desk_wifi_submit_get(desk_dispatcher_t *dispatcher,
                                        uint32_t peer_token, bool authorized)
{
    if (dispatcher == NULL || dispatcher->core == NULL || peer_token == 0u)
        return DESK_WIFI_BAD_REQUEST;
    if (!authorized) return DESK_WIFI_UNAUTHORIZED;
    const desk_dispatch_status_t status = desk_dispatcher_submit_status(
        dispatcher, DESK_CHANNEL_WIFI, peer_token, true);
    if (status == DESK_DISPATCH_BUSY) return DESK_WIFI_BUSY;
    if (status != DESK_DISPATCH_QUEUED) return DESK_WIFI_BAD_REQUEST;
    return DESK_WIFI_QUEUED;
}

bool desk_wifi_match_ack(uint32_t expected_request_id,
                         const uint8_t *ack, size_t ack_size,
                         desk_ack_t *decoded)
{
    if (decoded == NULL || expected_request_id == 0u) return false;
    desk_ack_t result = {0};
    if (desk_wire_decode_ack(ack, ack_size, &result) != DESK_WIRE_OK ||
        result.request_id != expected_request_id)
        return false;
    *decoded = result;
    return true;
}
