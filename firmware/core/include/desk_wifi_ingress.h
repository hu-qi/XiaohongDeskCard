#ifndef XIAOHONG_DESK_WIFI_INGRESS_H
#define XIAOHONG_DESK_WIFI_INGRESS_H

#include "desk_dispatcher.h"
#include "desk_http_json.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Wi-Fi HTTP application ingress. Real HTTP server/socket/TLS/bearer auth
 * remain platform responsibilities. One request is one unique nonzero peer
 * token; tokens must not be reused while ACK messages are in flight.
 */
typedef enum {
    DESK_WIFI_QUEUED = 0,
    DESK_WIFI_BAD_REQUEST,
    DESK_WIFI_UNAUTHORIZED,
    DESK_WIFI_BUSY
} desk_wifi_result_t;

/*
 * Must run on Wi-Fi HTTP worker, not in ISR. authorized may become true only
 * after trusted HTTP identity/token checks. Caller owns the raw body buffer.
 * request_id is written ONLY when queued.
 */
desk_wifi_result_t desk_wifi_submit_put(desk_dispatcher_t *dispatcher,
                                        uint32_t peer_token, bool authorized,
                                        const uint8_t *body, size_t body_size,
                                        uint32_t *request_id);

/* Validate UI-task ACK before responding to the originating HTTP request. */
bool desk_wifi_match_ack(uint32_t expected_request_id,
                         const uint8_t *ack, size_t ack_size,
                         desk_ack_t *decoded);

#endif
