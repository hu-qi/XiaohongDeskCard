#include "desk_reply_mailbox.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    desk_reply_mailbox_t mailbox;
    unsigned locks;
    unsigned unlocks;
    unsigned wakes;
    bool held;
} fixture_t;

static void lock(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(!f->held);
    f->held = true;
    f->locks++;
}
static void unlock(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(f->held);
    f->held = false;
    f->unlocks++;
}
static void notify(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(!f->held); /* notifier must never be called under mailbox lock */
    f->wakes++;
}

static void push_ack(fixture_t *f, desk_channel_t channel,
                     uint32_t peer, uint32_t request)
{
    desk_ack_t ack = {request, request, DESK_OK};
    uint8_t encoded[DESK_ACK_SIZE] = {0};
    assert(desk_wire_encode_ack(&ack, encoded, sizeof(encoded)) == DESK_WIRE_OK);
    assert(desk_reply_push_ack(&f->mailbox, channel, peer, encoded));
}

static void assert_ack(const desk_reply_item_t *item,
                       desk_channel_t channel, uint32_t peer, uint32_t request)
{
    assert(item->kind == DESK_REPLY_ACK);
    assert(item->channel == channel && item->peer_token == peer);
    desk_ack_t decoded = {0};
    assert(desk_wire_decode_ack(item->payload.ack,
                                DESK_ACK_SIZE, &decoded) == DESK_WIRE_OK);
    assert(decoded.request_id == request);
}

static desk_http_state_t status(void)
{
    desk_http_state_t state = {0};
    state.revision = 11u;
    strcpy(state.content.name, "张三");
    strcpy(state.content.title, "开发者");
    strcpy(state.content.organization, "OpenHarmony");
    strcpy(state.content.theme, "classic");
    state.display = state.content;
    return state;
}

static void assert_no_vacant_sensitive_data(const desk_reply_mailbox_t *box)
{
    for (unsigned slot = 0; slot < DESK_REPLY_CAPACITY; ++slot) {
        bool active = false;
        for (unsigned i = 0; i < box->count; ++i) {
            if ((box->head + i) % DESK_REPLY_CAPACITY == slot) active = true;
        }
        if (!active) {
            const uint8_t *bytes = (const uint8_t *)&box->queue[slot];
            for (size_t i = 0; i < sizeof(box->queue[slot]); ++i)
                assert(bytes[i] == 0u);
        }
    }
}

int main(void)
{
    fixture_t f = {0};
    desk_reply_hooks_t hooks = {lock, unlock, notify, &f};
    assert(desk_reply_init(&f.mailbox, hooks));

    /* Four different communication sessions all have pending results. */
    push_ack(&f, DESK_CHANNEL_BLE, 11u, 101u);
    desk_http_state_t state = status();
    assert(desk_reply_push_status(&f.mailbox, DESK_CHANNEL_WIFI, 22u,
                                  DESK_OK, &state));
    push_ack(&f, DESK_CHANNEL_SLE, 33u, 103u);
    push_ack(&f, DESK_CHANNEL_BLE, 44u, 104u);
    assert(desk_reply_pending(&f.mailbox) == 4u);
    assert(f.wakes == 4u);

    desk_reply_item_t got = {0};
    /* Independent SLE consumer runs first, but BLE and HTTP MUST stay queued. */
    assert(!desk_reply_take_for(&f.mailbox, DESK_CHANNEL_SLE, 22u, &got));
    assert(!desk_reply_take_for(&f.mailbox, DESK_CHANNEL_WIFI, 33u, &got));
    assert(desk_reply_pending(&f.mailbox) == 4u);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_SLE, 33u, &got));
    assert_ack(&got, DESK_CHANNEL_SLE, 33u, 103u);
    assert(desk_reply_pending(&f.mailbox) == 3u);
    assert_no_vacant_sensitive_data(&f.mailbox);

    /* A disconnected original BLE session must not poison another peer. */
    assert(desk_reply_discard_peer(&f.mailbox, DESK_CHANNEL_BLE, 11u) == 1u);
    assert(desk_reply_discard_peer(&f.mailbox, DESK_CHANNEL_BLE, 11u) == 0u);
    assert(!desk_reply_take_for(&f.mailbox, DESK_CHANNEL_BLE, 11u, &got));
    assert(desk_reply_pending(&f.mailbox) == 2u);
    assert_no_vacant_sensitive_data(&f.mailbox);

    /* Wi-Fi HTTP worker retrieves only its own snapshot. */
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_WIFI, 22u, &got));
    assert(got.kind == DESK_REPLY_STATUS && got.has_state);
    assert(got.payload.state.revision == 11u);
    assert(strcmp(got.payload.state.content.name, "张三") == 0);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_BLE, 44u, &got));
    assert_ack(&got, DESK_CHANNEL_BLE, 44u, 104u);
    assert(desk_reply_pending(&f.mailbox) == 0u);
    assert_no_vacant_sensitive_data(&f.mailbox);

    /* Force wraparound, then remove middle/first/last across the ring edge. */
    push_ack(&f, DESK_CHANNEL_BLE, 1u, 1u);
    push_ack(&f, DESK_CHANNEL_SLE, 2u, 2u);
    push_ack(&f, DESK_CHANNEL_WIFI, 3u, 3u);
    assert(desk_reply_pop(&f.mailbox, &got)); /* single consumer only */
    assert_ack(&got, DESK_CHANNEL_BLE, 1u, 1u);
    assert(desk_reply_pop(&f.mailbox, &got));
    assert_ack(&got, DESK_CHANNEL_SLE, 2u, 2u);
    /* head == 2 now; pushing 3 more wraps into physical slots 0 and 1. */
    push_ack(&f, DESK_CHANNEL_BLE, 4u, 4u);
    push_ack(&f, DESK_CHANNEL_SLE, 5u, 5u);
    push_ack(&f, DESK_CHANNEL_WIFI, 6u, 6u);
    assert(f.mailbox.head == 2u);
    assert(desk_reply_pending(&f.mailbox) == 4u);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_SLE, 5u, &got));
    assert_ack(&got, DESK_CHANNEL_SLE, 5u, 5u);
    assert_no_vacant_sensitive_data(&f.mailbox);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_WIFI, 6u, &got));
    assert_ack(&got, DESK_CHANNEL_WIFI, 6u, 6u);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_BLE, 4u, &got));
    assert_ack(&got, DESK_CHANNEL_BLE, 4u, 4u);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_WIFI, 3u, &got));
    assert_ack(&got, DESK_CHANNEL_WIFI, 3u, 3u);
    assert(desk_reply_pending(&f.mailbox) == 0u);
    assert_no_vacant_sensitive_data(&f.mailbox);

    /* Disconnection drops multiple replies for one peer, NOT other channels
     * or a new generation with a different token. */
    push_ack(&f, DESK_CHANNEL_SLE, 77u, 10u);
    push_ack(&f, DESK_CHANNEL_BLE, 77u, 11u);
    push_ack(&f, DESK_CHANNEL_SLE, 88u, 12u);
    push_ack(&f, DESK_CHANNEL_SLE, 77u, 13u);
    assert(desk_reply_discard_peer(&f.mailbox, DESK_CHANNEL_SLE, 77u) == 2u);
    assert(desk_reply_pending(&f.mailbox) == 2u);
    assert_no_vacant_sensitive_data(&f.mailbox);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_BLE, 77u, &got));
    assert_ack(&got, DESK_CHANNEL_BLE, 77u, 11u);
    assert(desk_reply_take_for(&f.mailbox, DESK_CHANNEL_SLE, 88u, &got));
    assert_ack(&got, DESK_CHANNEL_SLE, 88u, 12u);

    /* A wrong token/channel, invalid parameters, or empty mailbox leaves
     * all remaining messages untouched. */
    assert(!desk_reply_take_for(&f.mailbox, DESK_CHANNEL_BLE, 0u, &got));
    assert(!desk_reply_take_for(&f.mailbox, (desk_channel_t)99, 1u, &got));
    assert(!desk_reply_take_for(&f.mailbox, DESK_CHANNEL_SLE, 77u, NULL));
    assert(desk_reply_discard_peer(&f.mailbox, DESK_CHANNEL_WIFI, 0u) == 0u);
    assert(desk_reply_pending(&f.mailbox) == 0u);
    assert(f.locks == f.unlocks && !f.held);
    assert(f.wakes == 12u);
    puts("PASS: per-connection reply take/discard, out-of-order worker scheduling,"
         " ring wrap, memory erase and channel generation isolation");
    return 0;
}
