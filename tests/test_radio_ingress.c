#include "desk_radio_ingress.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned displays;
    unsigned saves;
    unsigned wakes;
    unsigned replies;
    struct {
        desk_channel_t channel;
        uint32_t peer;
        uint8_t bytes[DESK_ACK_SIZE];
    } replies_log[16];
} fixture_t;

static int display(void *ctx, const desk_content_t *content)
{
    fixture_t *state = (fixture_t *)ctx;
    assert(strcmp(content->name, "李四") == 0 ||
           strcmp(content->name, "张三") == 0);
    state->displays++;
    return 1;
}

static int persist(void *ctx, const desk_content_t *content, uint32_t revision)
{
    fixture_t *state = (fixture_t *)ctx;
    assert(content != NULL && revision > 0u);
    state->saves++;
    return 1;
}

static void wake(void *ctx)
{
    fixture_t *state = (fixture_t *)ctx;
    state->wakes++;
}

static void reply(void *ctx, desk_channel_t channel, uint32_t peer,
                  const uint8_t ack[DESK_ACK_SIZE])
{
    fixture_t *state = (fixture_t *)ctx;
    assert(state->replies < 16u);
    const unsigned i = state->replies++;
    state->replies_log[i].channel = channel;
    state->replies_log[i].peer = peer;
    memcpy(state->replies_log[i].bytes, ack, DESK_ACK_SIZE);
}

static desk_set_command_t command(uint32_t id, uint32_t revision,
                                  const char *name)
{
    desk_set_command_t cmd = {0};
    cmd.request_id = id;
    cmd.expected_revision = revision;
    strcpy(cmd.content.name, name);
    strcpy(cmd.content.title, "技术负责人");
    strcpy(cmd.content.organization, "OpenHarmony");
    strcpy(cmd.content.theme, "classic");
    return cmd;
}

static void begin_for(uint8_t packet[7], const uint8_t *frame, size_t size)
{
    const uint32_t crc = desk_wire_crc32(frame, size);
    packet[0] = DESK_PACKET_BEGIN;
    packet[1] = (uint8_t)size;
    packet[2] = (uint8_t)(size >> 8u);
    packet[3] = (uint8_t)crc;
    packet[4] = (uint8_t)(crc >> 8u);
    packet[5] = (uint8_t)(crc >> 16u);
    packet[6] = (uint8_t)(crc >> 24u);
}

static desk_radio_result_t submit_frame(desk_radio_link_t *link,
                                        const desk_set_command_t *command_value,
                                        uint32_t now)
{
    uint8_t frame[DESK_WIRE_MAX_FRAME] = {0};
    size_t length = 0u;
    assert(desk_wire_encode_set(command_value, frame, sizeof(frame), &length)
           == DESK_WIRE_OK);

    uint8_t begin[7] = {0};
    begin_for(begin, frame, length);
    assert(desk_radio_link_receive(link, begin, sizeof(begin), now++, 500u)
           == DESK_RADIO_PROGRESS);

    size_t offset = 0u;
    while (offset < length) {
        uint8_t part[20] = {0};
        size_t amount = length - offset;
        if (amount > 17u) amount = 17u;
        part[0] = DESK_PACKET_CHUNK;
        part[1] = (uint8_t)offset;
        part[2] = (uint8_t)(offset >> 8u);
        memcpy(part + 3, frame + offset, amount);
        assert(desk_radio_link_receive(link, part, amount + 3u, now++, 500u)
               == DESK_RADIO_PROGRESS);
        offset += amount;
    }
    const uint8_t commit = DESK_PACKET_COMMIT;
    return desk_radio_link_receive(link, &commit, 1u, now++, 500u);
}

int main(void)
{
    fixture_t fixture = {0};
    desk_core_t core;
    const desk_hooks_t core_hooks = {NULL, persist, display, &fixture};
    desk_core_init(&core, core_hooks);

    desk_dispatcher_t queue = {0};
    desk_dispatch_hooks_t hooks = {0};
    hooks.notify_ui = wake;
    hooks.on_result = reply;
    hooks.context = &fixture;
    assert(desk_dispatcher_init(&queue, &core, hooks));

    desk_radio_link_t ble = {0};
    desk_radio_link_t sle = {0};
    assert(!desk_radio_link_open(&ble, &queue, DESK_CHANNEL_WIFI, 1u));
    assert(!desk_radio_link_open(&ble, &queue, DESK_CHANNEL_BLE, 0u));
    assert(desk_radio_link_open(&ble, &queue, DESK_CHANNEL_BLE, 101u));
    assert(desk_radio_link_open(&sle, &queue, DESK_CHANNEL_SLE, 202u));
    assert(!desk_radio_link_open(&ble, &queue, DESK_CHANNEL_BLE, 303u));

    const desk_set_command_t cmd_ble = command(11u, 0u, "张三");
    const desk_set_command_t cmd_sle = command(22u, 1u, "李四");
    uint8_t frame[DESK_WIRE_MAX_FRAME] = {0};
    size_t frame_size = 0u;
    assert(desk_wire_encode_set(&cmd_ble, frame, sizeof(frame), &frame_size)
           == DESK_WIRE_OK);
    uint8_t begin[7] = {0};
    begin_for(begin, frame, frame_size);

    /* A discovered device is untrusted; GATT/SSAP writes require binding. */
    assert(desk_radio_link_receive(&ble, begin, sizeof(begin), 1u, 500u)
           == DESK_RADIO_NOT_AUTHORIZED);
    assert(!ble.packet.rx.active);

    desk_radio_link_set_authorized(&ble, true);
    desk_radio_link_set_authorized(&sle, true);

    assert(submit_frame(&ble, &cmd_ble, 10u) == DESK_RADIO_QUEUED);
    assert(submit_frame(&sle, &cmd_sle, 100u) == DESK_RADIO_QUEUED);
    assert(fixture.displays == 0u);
    assert(desk_dispatcher_pending(&queue) == 2u);
    assert(ble.awaiting_ack && sle.awaiting_ack);

    /* No second transaction until a matching business ACK is handled. */
    assert(desk_radio_link_receive(&ble, begin, sizeof(begin), 200u, 500u)
           == DESK_RADIO_AWAITING_ACK);
    assert(desk_dispatcher_process_one(&queue));
    assert(desk_dispatcher_process_one(&queue));
    assert(core.revision == 2u);
    assert(fixture.displays == 2u && fixture.saves == 2u);
    assert(fixture.replies == 2u);

    desk_ack_t decoded = {0};
    assert(desk_wire_decode_ack(fixture.replies_log[0].bytes, DESK_ACK_SIZE,
                                &decoded) == DESK_WIRE_OK);
    assert(decoded.request_id == 11u && decoded.revision == 1u &&
           decoded.result == DESK_OK);
    assert(fixture.replies_log[0].channel == DESK_CHANNEL_BLE &&
           fixture.replies_log[0].peer == 101u);
    assert(fixture.replies_log[1].channel == DESK_CHANNEL_SLE &&
           fixture.replies_log[1].peer == 202u);

    assert(!desk_radio_link_ack_complete(&ble, 999u,
                                        fixture.replies_log[0].bytes, DESK_ACK_SIZE));
    assert(!desk_radio_link_ack_complete(&ble, 101u,
                                        fixture.replies_log[1].bytes, DESK_ACK_SIZE));
    assert(desk_radio_link_ack_complete(&ble, 101u,
                                        fixture.replies_log[0].bytes, DESK_ACK_SIZE));
    assert(desk_radio_link_ack_complete(&sle, 202u,
                                        fixture.replies_log[1].bytes, DESK_ACK_SIZE));
    assert(!ble.awaiting_ack && !sle.awaiting_ack);

    /* Authorization can be revoked; incomplete fragments are discarded. */
    assert(desk_radio_link_receive(&ble, begin, sizeof(begin), 300u, 500u)
           == DESK_RADIO_PROGRESS);
    desk_radio_link_set_authorized(&ble, false);
    assert(!ble.packet.rx.active);
    assert(desk_radio_link_receive(&ble, begin, sizeof(begin), 301u, 500u)
           == DESK_RADIO_NOT_AUTHORIZED);
    desk_radio_link_set_authorized(&ble, true);
    assert(desk_radio_link_receive(&ble, begin, sizeof(begin), 302u, 500u)
           == DESK_RADIO_PROGRESS);
    desk_radio_link_close(&ble);
    assert(desk_radio_link_receive(&ble, begin, sizeof(begin), 303u, 500u)
           == DESK_RADIO_NOT_CONNECTED);

    /* A reconnect MUST use a fresh per-connection peer token. */
    assert(desk_radio_link_open(&ble, &queue, DESK_CHANNEL_BLE, 303u));
    desk_radio_link_set_authorized(&ble, true);
    const uint8_t commit = DESK_PACKET_COMMIT;
    assert(desk_radio_link_receive(&ble, &commit, 1u, 304u, 500u)
           == DESK_RADIO_BAD_PACKET);
    assert(!desk_radio_link_ack_complete(&ble, 101u,
                                        fixture.replies_log[0].bytes, DESK_ACK_SIZE));

    /* Corrupt full-frame payload cannot reach the Desk owner task. */
    begin_for(begin, frame, frame_size);
    assert(desk_radio_link_receive(&ble, begin, 7u, 400u, 500u)
           == DESK_RADIO_PROGRESS);
    uint8_t altered[DESK_WIRE_MAX_FRAME + 3] = {DESK_PACKET_CHUNK};
    memcpy(altered + 3, frame, frame_size);
    altered[frame_size + 2u] ^= 0x10u;
    assert(desk_radio_link_receive(&ble, altered, frame_size + 3u, 401u, 500u)
           == DESK_RADIO_PROGRESS);
    assert(desk_radio_link_receive(&ble, &commit, 1u, 402u, 500u)
           == DESK_RADIO_BAD_PACKET);
    assert(core.revision == 2u && fixture.replies == 2u);

    /* Queue backpressure is surfaced, not reported as a successful update. */
    for (unsigned i = 0; i < DESK_DISPATCH_QUEUE_CAPACITY; ++i) {
        desk_dispatch_item_t item = {0};
        item.channel = DESK_CHANNEL_WIFI;
        item.peer_token = 500u + i;
        item.authorized = false; /* no unintended state modifications */
        item.command = command(40u + i, 2u, "张三");
        assert(desk_dispatcher_submit(&queue, &item) == DESK_DISPATCH_QUEUED);
    }
    desk_set_command_t command_full = command(99u, 2u, "张三");
    assert(submit_frame(&ble, &command_full, 500u) == DESK_RADIO_QUEUE_FULL);
    assert(!ble.awaiting_ack);
    assert(desk_dispatcher_pending(&queue) == DESK_DISPATCH_QUEUE_CAPACITY);
    while (desk_dispatcher_process_one(&queue)) {}
    assert(core.revision == 2u);

    /* After backpressure clears, ingress can safely retry the entire frame. */
    desk_set_command_t after = command(100u, 2u, "李四");
    assert(submit_frame(&ble, &after, 600u) == DESK_RADIO_QUEUED);
    assert(desk_dispatcher_process_one(&queue));
    assert(core.revision == 3u && fixture.displays == 3u);

    desk_radio_link_close(&ble);
    desk_radio_link_close(&sle);
    puts("PASS: radio ingress pairing, BLE+SLE, queue, CRC, disconnect, stale ACK");
    return 0;
}
