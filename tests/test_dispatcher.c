#include "desk_dispatcher.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned locks;
    unsigned unlocks;
    unsigned wakes;
    unsigned callbacks;
    unsigned renders;
    unsigned saves;
    bool is_locked;
    uint32_t revoked_peer;
    desk_channel_t channels[16];
    uint32_t peers[16];
    desk_ack_t acks[16];
} fake_context_t;

static void lock(void *raw)
{
    fake_context_t *fake = (fake_context_t *)raw;
    assert(!fake->is_locked);
    fake->is_locked = true;
    fake->locks++;
}

static void unlock(void *raw)
{
    fake_context_t *fake = (fake_context_t *)raw;
    assert(fake->is_locked);
    fake->is_locked = false;
    fake->unlocks++;
}

static void notify_ui(void *raw)
{
    fake_context_t *fake = (fake_context_t *)raw;
    assert(!fake->is_locked);
    fake->wakes++;
}

static void on_result(void *raw, desk_channel_t channel,
                      uint32_t peer_token, const uint8_t ack[DESK_ACK_SIZE])
{
    fake_context_t *fake = (fake_context_t *)raw;
    assert(!fake->is_locked);
    assert(fake->callbacks < 16u);
    const unsigned index = fake->callbacks++;
    fake->channels[index] = channel;
    fake->peers[index] = peer_token;
    assert(desk_wire_decode_ack(ack, DESK_ACK_SIZE,
                                &fake->acks[index]) == DESK_WIRE_OK);
}

static int save(void *raw, const desk_content_t *content, uint32_t revision)
{
    fake_context_t *fake = (fake_context_t *)raw;
    assert(!fake->is_locked);
    assert(content != NULL && revision != 0u);
    fake->saves++;
    return 1;
}

static int render(void *raw, const desk_content_t *content)
{
    fake_context_t *fake = (fake_context_t *)raw;
    assert(!fake->is_locked);
    assert(content != NULL);
    fake->renders++;
    return 1;
}

static bool check_authorized(void *raw, desk_channel_t channel, uint32_t peer)
{
    fake_context_t *fake = (fake_context_t *)raw;
    (void)channel;
    return peer != fake->revoked_peer;
}

static desk_dispatch_item_t make_item(desk_channel_t channel, uint32_t peer,
                                      uint32_t request, uint32_t expected,
                                      bool authorized)
{
    desk_dispatch_item_t item = {0};
    item.channel = channel;
    item.peer_token = peer;
    item.authorized = authorized;
    item.command.request_id = request;
    item.command.expected_revision = expected;
    strcpy(item.command.content.name, "小鸿");
    strcpy(item.command.content.title, "桌牌");
    strcpy(item.command.content.organization, "OpenHarmony");
    strcpy(item.command.content.theme, "classic");
    return item;
}

int main(void)
{
    fake_context_t fake = {0};
    desk_core_t core;
    desk_hooks_t core_hooks = {NULL, save, render, &fake};
    desk_core_init(&core, core_hooks);
    desk_dispatch_hooks_t hooks = {lock, unlock, notify_ui, on_result, &fake,
                                   check_authorized};
    desk_dispatcher_t dispatcher;
    assert(desk_dispatcher_init(&dispatcher, &core, hooks));
    assert(!desk_dispatcher_process_one(&dispatcher));
    assert(desk_dispatcher_pending(&dispatcher) == 0u);

    /* Three separate transport workers arrive before UI task is scheduled. */
    desk_dispatch_item_t ble = make_item(DESK_CHANNEL_BLE, 100u, 1u, 0u, true);
    desk_dispatch_item_t sle = make_item(DESK_CHANNEL_SLE, 200u, 2u, 1u, true);
    desk_dispatch_item_t wifi = make_item(DESK_CHANNEL_WIFI, 300u, 3u, 2u, true);
    assert(desk_dispatcher_submit(&dispatcher, &ble) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_submit(&dispatcher, &sle) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_submit(&dispatcher, &wifi) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_pending(&dispatcher) == 3u);
    assert(fake.renders == 0u && fake.callbacks == 0u);
    while (desk_dispatcher_process_one(&dispatcher)) {}
    assert(desk_dispatcher_pending(&dispatcher) == 0u);
    assert(core.revision == 3u && fake.saves == 3u && fake.renders == 3u);
    assert(fake.callbacks == 3u);
    assert(fake.acks[0].request_id == 1u && fake.acks[0].revision == 1u);
    assert(fake.acks[1].request_id == 2u && fake.acks[1].revision == 2u);
    assert(fake.acks[2].request_id == 3u && fake.acks[2].revision == 3u);
    assert(fake.channels[0] == DESK_CHANNEL_BLE && fake.peers[0] == 100u);
    assert(fake.channels[1] == DESK_CHANNEL_SLE && fake.peers[1] == 200u);
    assert(fake.channels[2] == DESK_CHANNEL_WIFI && fake.peers[2] == 300u);
    assert(fake.acks[0].result == DESK_OK &&
           fake.acks[1].result == DESK_OK &&
           fake.acks[2].result == DESK_OK);

    /* Unauthorized and stale revision never change the display. */
    desk_dispatch_item_t denied = make_item(DESK_CHANNEL_BLE, 110u, 4u, 3u, false);
    desk_dispatch_item_t stale = make_item(DESK_CHANNEL_SLE, 210u, 5u, 0u, true);
    assert(desk_dispatcher_submit(&dispatcher, &denied) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_submit(&dispatcher, &stale) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(core.revision == 3u && fake.renders == 3u);
    assert(fake.acks[3].result == DESK_UNAUTHORIZED);
    assert(fake.acks[4].result == DESK_CONFLICT);

    /* Full bounded queue responds BUSY, without heap allocation or overwrite. */
    for (unsigned i = 0; i < DESK_DISPATCH_QUEUE_CAPACITY; ++i) {
        desk_dispatch_item_t item = make_item(DESK_CHANNEL_WIFI,
                                              400u + i, 10u + i, 3u + i, true);
        assert(desk_dispatcher_submit(&dispatcher, &item) == DESK_DISPATCH_QUEUED);
    }
    desk_dispatch_item_t overflow =
        make_item(DESK_CHANNEL_WIFI, 999u, 99u, 3u, true);
    assert(desk_dispatcher_submit(&dispatcher, &overflow) == DESK_DISPATCH_BUSY);
    assert(desk_dispatcher_pending(&dispatcher) == DESK_DISPATCH_QUEUE_CAPACITY);
    while (desk_dispatcher_process_one(&dispatcher)) {}
    assert(core.revision == 7u);
    assert(fake.acks[8].request_id == 13u);

    /* Invalid channels and missing peer tokens rejected at ingress. */
    desk_dispatch_item_t invalid = make_item((desk_channel_t)99, 22u, 42u, 7u, true);
    assert(desk_dispatcher_submit(&dispatcher, &invalid) == DESK_DISPATCH_INVALID);
    invalid = make_item(DESK_CHANNEL_SLE, 0u, 42u, 7u, true);
    assert(desk_dispatcher_submit(&dispatcher, &invalid) == DESK_DISPATCH_INVALID);

    /* Auth must be revalidated after queueing, at actual UI execution. */
    desk_dispatch_item_t revoked =
        make_item(DESK_CHANNEL_SLE, 777u, 20u, 7u, true);
    assert(desk_dispatcher_submit(&dispatcher, &revoked) == DESK_DISPATCH_QUEUED);
    fake.revoked_peer = 777u;
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(fake.acks[9].request_id == 20u &&
           fake.acks[9].result == DESK_UNAUTHORIZED);
    assert(core.revision == 7u && fake.renders == 7u);

    assert(fake.locks == fake.unlocks && !fake.is_locked);
    assert(fake.wakes == 10u);
    assert(fake.callbacks == 10u);
    puts("PASS: UI-thread serialization, FIFO, 3 transports, ACK, authorization, overflow");
    return 0;
}
