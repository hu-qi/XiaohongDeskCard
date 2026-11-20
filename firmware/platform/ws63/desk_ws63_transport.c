#include "desk_ws63_transport.h"

#include <string.h>

#include "desk_http_json.h"
#include "desk_wifi_ingress.h"

/* Generations start at 1 so that 0 always means "no live connection". */
static uint32_t next_generation(uint32_t current)
{
    uint32_t next = current + 1u;
    if (next == 0u) next = 1u;
    return next;
}

static void box_lock(void *ctx)
{
    desk_transport_t *t = (desk_transport_t *)ctx;
    if (t != NULL && t->lock_fn != NULL) t->lock_fn(t->lock_ctx);
}

static void box_unlock(void *ctx)
{
    desk_transport_t *t = (desk_transport_t *)ctx;
    if (t != NULL && t->unlock_fn != NULL) t->unlock_fn(t->lock_ctx);
}

static void notify_worker(void *ctx)
{
    (void)ctx;
    /* The vendor worker tasks poll their own replies; nothing to wake here.
     * A platform with a dedicated comms task would post to its queue. */
}

bool desk_transport_init(desk_transport_t *transport,
                         const desk_transport_config_t *config)
{
    if (transport == NULL || config == NULL || config->dispatcher == NULL ||
        config->core == NULL)
        return false;

    memset(transport, 0, sizeof(*transport));
    transport->dispatcher = config->dispatcher;
    transport->core = config->core;
    transport->lock_fn = config->lock;
    transport->unlock_fn = config->unlock;
    transport->lock_ctx = config->lock_ctx;

    desk_reply_hooks_t hooks = {box_lock, box_unlock, notify_worker, transport};
    if (!desk_reply_init(&transport->replies, hooks)) return false;

    transport->ready = true;
    return true;
}

/* ---------------- BLE ---------------- */

uint32_t desk_transport_ble_open(desk_transport_t *transport)
{
    if (transport == NULL || !transport->ready) return 0u;
    transport->ble_generation = next_generation(transport->ble_generation);
    const uint32_t gen = transport->ble_generation;
    /* A fresh connection is not authorized until the ownership handshake. */
    if (!desk_radio_link_open(&transport->ble, transport->dispatcher,
                              DESK_CHANNEL_BLE, gen)) {
        transport->ble_generation = 0u;
        return 0u;
    }
    return gen;
}

void desk_transport_ble_close(desk_transport_t *transport, uint32_t generation)
{
    if (transport == NULL || !transport->ready) return;
    if (generation != transport->ble_generation) return;
    desk_radio_link_close(&transport->ble);
    (void)desk_reply_discard_peer(&transport->replies, DESK_CHANNEL_BLE,
                                  generation);
    transport->ble_generation = 0u;
}

desk_radio_result_t desk_transport_ble_write(desk_transport_t *transport,
                                             uint32_t generation,
                                             const uint8_t *packet,
                                             size_t size, uint32_t now_ms)
{
    if (transport == NULL || !transport->ready || generation == 0u ||
        generation != transport->ble_generation)
        return DESK_RADIO_NOT_CONNECTED;
    return desk_radio_link_receive(&transport->ble, packet, size, now_ms,
                                   DESK_TRANSPORT_TIMEOUT_MS);
}

bool desk_transport_ble_take_ack(desk_transport_t *transport,
                                 uint32_t generation, uint8_t out[16])
{
    if (transport == NULL || !transport->ready || out == NULL ||
        generation != transport->ble_generation)
        return false;
    desk_reply_item_t item;
    if (!desk_reply_take_for(&transport->replies, DESK_CHANNEL_BLE, generation,
                             &item))
        return false;
    if (item.kind != DESK_REPLY_ACK) return false;
    memcpy(out, item.payload.ack, DESK_ACK_SIZE);
    return true;
}

/* ---------------- SLE ---------------- */

uint32_t desk_transport_sle_open(desk_transport_t *transport)
{
    if (transport == NULL || !transport->ready) return 0u;
    transport->sle_generation = next_generation(transport->sle_generation);
    const uint32_t gen = transport->sle_generation;
    if (!desk_radio_link_open(&transport->sle, transport->dispatcher,
                              DESK_CHANNEL_SLE, gen)) {
        transport->sle_generation = 0u;
        return 0u;
    }
    return gen;
}

void desk_transport_sle_close(desk_transport_t *transport, uint32_t generation)
{
    if (transport == NULL || !transport->ready) return;
    if (generation != transport->sle_generation) return;
    desk_radio_link_close(&transport->sle);
    (void)desk_reply_discard_peer(&transport->replies, DESK_CHANNEL_SLE,
                                  generation);
    transport->sle_generation = 0u;
}

desk_radio_result_t desk_transport_sle_write(desk_transport_t *transport,
                                             uint32_t generation,
                                             const uint8_t *packet,
                                             size_t size, uint32_t now_ms)
{
    if (transport == NULL || !transport->ready || generation == 0u ||
        generation != transport->sle_generation)
        return DESK_RADIO_NOT_CONNECTED;
    return desk_radio_link_receive(&transport->sle, packet, size, now_ms,
                                   DESK_TRANSPORT_TIMEOUT_MS);
}

bool desk_transport_sle_take_ack(desk_transport_t *transport,
                                 uint32_t generation, uint8_t out[16])
{
    if (transport == NULL || !transport->ready || out == NULL ||
        generation != transport->sle_generation)
        return false;
    desk_reply_item_t item;
    if (!desk_reply_take_for(&transport->replies, DESK_CHANNEL_SLE, generation,
                             &item))
        return false;
    if (item.kind != DESK_REPLY_ACK) return false;
    memcpy(out, item.payload.ack, DESK_ACK_SIZE);
    return true;
}

/* ---------------- HTTP ---------------- */

bool desk_transport_set_http_token(desk_transport_t *transport,
                                   const char *token, size_t len)
{
    if (transport == NULL || !transport->ready || token == NULL) return false;
    if (len == 0u || len >= DESK_HTTP_TOKEN_LEN) return false;
    memcpy(transport->http_token, token, len);
    transport->http_token[len] = '\0';
    return true;
}

bool desk_transport_http_authorized(const desk_transport_t *transport,
                                    const char *bearer, size_t len)
{
    if (transport == NULL || !transport->ready) return false;
    if (transport->http_token[0] == '\0') return false;
    const size_t want = strlen(transport->http_token);
    if (len != want || bearer == NULL) return false;
    /* Constant-time compare keeps the token out of timing side channels. */
    unsigned diff = 0u;
    for (size_t i = 0; i < want; i++)
        diff |= (unsigned)(uint8_t)transport->http_token[i] ^
                (unsigned)(uint8_t)bearer[i];
    return diff == 0u;
}

int desk_transport_http_put(desk_transport_t *transport,
                            const char *bearer, size_t bearer_len,
                            const uint8_t *body, size_t body_len,
                            uint32_t *request_id)
{
    if (transport == NULL || !transport->ready || body == NULL ||
        request_id == NULL)
        return -400;
    if (!desk_transport_http_authorized(transport, bearer, bearer_len))
        return -401;

    /* One shared counter for PUT and GET: two independent counters would both
     * start at 1 and collide, so a GET could collect the PUT's ACK. */
    transport->http_generation = next_generation(transport->http_generation);
    const uint32_t peer = transport->http_generation;

    const desk_wifi_result_t result =
        desk_wifi_submit_put(transport->dispatcher, peer, true, body,
                             body_len, request_id);
    switch (result) {
        case DESK_WIFI_QUEUED: return 0;
        case DESK_WIFI_UNAUTHORIZED: return -401;
        case DESK_WIFI_BUSY: return -429;
        case DESK_WIFI_BAD_REQUEST:
        default: return -400;
    }
}

int desk_transport_http_get(desk_transport_t *transport,
                            const char *bearer, size_t bearer_len,
                            uint32_t *peer_token)
{
    if (transport == NULL || !transport->ready || peer_token == NULL)
        return -400;
    if (!desk_transport_http_authorized(transport, bearer, bearer_len))
        return -401;

    transport->http_generation = next_generation(transport->http_generation);
    const uint32_t peer = transport->http_generation;

    const desk_wifi_result_t result =
        desk_wifi_submit_get(transport->dispatcher, peer, true);
    if (result != DESK_WIFI_QUEUED) return result == DESK_WIFI_BUSY ? -429 : -400;
    *peer_token = peer;
    return 0;
}

bool desk_transport_http_take_reply(desk_transport_t *transport,
                                    uint32_t peer_token,
                                    desk_reply_item_t *out)
{
    if (transport == NULL || !transport->ready || out == NULL) return false;
    return desk_reply_take_for(&transport->replies, DESK_CHANNEL_WIFI,
                               peer_token, out);
}

void desk_transport_notify_worker(desk_transport_t *transport)
{
    (void)transport;
}
