#include "desk_reply_mailbox.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    desk_reply_mailbox_t mailbox;
    desk_content_t display;
    unsigned locks;
    unsigned unlocks;
    unsigned wakes;
    unsigned renders;
    bool held;
    bool authorized;
} fixture_t;

static void lock_box(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(!f->held);
    f->held = true;
    f->locks++;
}
static void unlock_box(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(f->held);
    f->held = false;
    f->unlocks++;
}
static void wake_worker(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(!f->held);
    f->wakes++;
}
static int render(void *ctx, const desk_content_t *content)
{
    fixture_t *f = (fixture_t *)ctx;
    f->display = *content;
    f->renders++;
    return 1;
}
static bool read_display(void *ctx, desk_content_t *out)
{
    fixture_t *f = (fixture_t *)ctx;
    *out = f->display;
    return true;
}
static bool authorize(void *ctx, desk_channel_t channel, uint32_t peer_token)
{
    fixture_t *f = (fixture_t *)ctx;
    (void)channel;
    return f->authorized && peer_token != 0u;
}
static void send_ack(void *ctx, desk_channel_t channel, uint32_t peer,
                     const uint8_t ack[DESK_ACK_SIZE])
{
    fixture_t *f = (fixture_t *)ctx;
    assert(desk_reply_push_ack(&f->mailbox, channel, peer, ack));
}
static void send_status(void *ctx, desk_channel_t channel, uint32_t peer,
                        desk_result_t result, const desk_http_state_t *state)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(desk_reply_push_status(&f->mailbox, channel, peer, result, state));
}

static desk_dispatch_item_t set_command(desk_channel_t channel, uint32_t peer,
                                        uint32_t request_id, uint32_t expected,
                                        const char *name)
{
    desk_dispatch_item_t item = {0};
    item.channel = channel;
    item.peer_token = peer;
    item.authorized = true;
    item.command.request_id = request_id;
    item.command.expected_revision = expected;
    strcpy(item.command.content.name, name);
    strcpy(item.command.content.title, "开发者");
    strcpy(item.command.content.organization, "OpenHarmony");
    strcpy(item.command.content.theme, "classic");
    return item;
}

static void assert_ack(const desk_reply_item_t *item, desk_channel_t channel,
                       uint32_t peer, uint32_t req_id, uint32_t revision)
{
    assert(item->kind == DESK_REPLY_ACK && item->channel == channel);
    assert(item->peer_token == peer && !item->has_state);
    desk_ack_t decoded = {0};
    assert(desk_wire_decode_ack(item->payload.ack, DESK_ACK_SIZE,
                                &decoded) == DESK_WIRE_OK);
    assert(decoded.request_id == req_id);
    assert(decoded.revision == revision);
    assert(decoded.result == DESK_OK);
}

int main(void)
{
    fixture_t f = {0};
    f.authorized = true;
    desk_reply_hooks_t reply_hooks = {lock_box, unlock_box, wake_worker, &f};
    assert(desk_reply_init(&f.mailbox, reply_hooks));
    desk_reply_hooks_t broken = reply_hooks;
    broken.unlock = NULL;
    desk_reply_mailbox_t unused = {0};
    assert(!desk_reply_init(&unused, broken));
    assert(!desk_reply_pop(&f.mailbox, NULL));

    desk_core_t core;
    desk_hooks_t core_hooks = {NULL, NULL, render, &f};
    desk_core_init(&core, core_hooks);
    assert(desk_core_repaint(&core) == DESK_OK);

    desk_dispatcher_t dispatcher;
    desk_dispatch_hooks_t hooks = {0};
    hooks.context = &f;
    hooks.authorize_on_ui = authorize;
    hooks.read_display_on_ui = read_display;
    hooks.on_result = send_ack;
    hooks.on_status = send_status;
    assert(desk_dispatcher_init(&dispatcher, &core, hooks));

    /* Interleave BLE WRITE / Wi-Fi GET / SLE WRITE / Wi-Fi GET.
     * All results are produced on UI task then queued for network workers. */
    desk_dispatch_item_t ble = set_command(DESK_CHANNEL_BLE, 101u, 1u, 0u, "张三");
    desk_dispatch_item_t sle = set_command(DESK_CHANNEL_SLE, 202u, 2u, 1u, "李四");
    assert(desk_dispatcher_submit(&dispatcher, &ble) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_submit_status(&dispatcher, DESK_CHANNEL_WIFI, 303u,
                                         true) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_submit(&dispatcher, &sle) == DESK_DISPATCH_QUEUED);
    assert(desk_dispatcher_submit_status(&dispatcher, DESK_CHANNEL_WIFI, 404u,
                                         true) == DESK_DISPATCH_QUEUED);
    while (desk_dispatcher_process_one(&dispatcher)) {}
    assert(core.revision == 2u);
    assert(desk_reply_pending(&f.mailbox) == 4u);
    assert(f.wakes == 4u);

    /* Full mailbox MUST not overwrite earlier results and must report loss.
     * WS63 network adapter must throttle/cancel/timeout and allow status
     * reconciliation rather than claiming successful ACK delivery. */
    desk_ack_t dummy_ack = {99u, 2u, DESK_OK};
    uint8_t raw[DESK_ACK_SIZE] = {0};
    assert(desk_wire_encode_ack(&dummy_ack, raw, sizeof(raw)) == DESK_WIRE_OK);
    assert(!desk_reply_push_ack(&f.mailbox, DESK_CHANNEL_BLE, 505u, raw));
    assert(desk_reply_rejected_full(&f.mailbox) == 1u);
    assert(desk_reply_pending(&f.mailbox) == 4u);

    desk_reply_item_t event = {0};
    assert(desk_reply_pop(&f.mailbox, &event));
    assert_ack(&event, DESK_CHANNEL_BLE, 101u, 1u, 1u);
    assert(desk_reply_pop(&f.mailbox, &event));
    assert(event.kind == DESK_REPLY_STATUS && event.channel == DESK_CHANNEL_WIFI);
    assert(event.peer_token == 303u && event.result == DESK_OK && event.has_state);
    assert(event.payload.state.revision == 1u);
    assert(strcmp(event.payload.state.display.name, "张三") == 0);

    assert(desk_reply_pop(&f.mailbox, &event));
    assert_ack(&event, DESK_CHANNEL_SLE, 202u, 2u, 2u);
    assert(desk_reply_pop(&f.mailbox, &event));
    assert(event.kind == DESK_REPLY_STATUS && event.peer_token == 404u);
    assert(event.payload.state.revision == 2u);
    assert(strcmp(event.payload.state.display.name, "李四") == 0);
    assert(!desk_reply_pop(&f.mailbox, &event));

    /* After authorization revoked while queued: callback contains NO data. */
    assert(desk_dispatcher_submit_status(&dispatcher, DESK_CHANNEL_WIFI,
                                         606u, true) == DESK_DISPATCH_QUEUED);
    f.authorized = false;
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(desk_reply_pop(&f.mailbox, &event));
    assert(event.kind == DESK_REPLY_STATUS && event.peer_token == 606u);
    assert(event.result == DESK_UNAUTHORIZED && !event.has_state);
    assert(event.payload.state.revision == 0u);
    assert(event.payload.state.content.name[0] == '\0');

    /* Reject invalid ACK CRC, no token and no state on successful GET. */
    raw[12] ^= 0x80u;
    assert(!desk_reply_push_ack(&f.mailbox, DESK_CHANNEL_BLE, 1u, raw));
    assert(!desk_reply_push_ack(&f.mailbox, DESK_CHANNEL_BLE, 0u, raw));
    assert(!desk_reply_push_status(&f.mailbox, DESK_CHANNEL_WIFI, 1u, DESK_OK, NULL));
    assert(!desk_reply_push_status(&f.mailbox, DESK_CHANNEL_WIFI, 0u,
                                   DESK_UNAUTHORIZED, NULL));
    assert(desk_reply_rejected_full(&f.mailbox) == 1u);
    assert(f.locks == f.unlocks && !f.held);
    puts("PASS: bounded ACK/status FIFO, peer routing, ordering, full and privacy");
    return 0;
}
