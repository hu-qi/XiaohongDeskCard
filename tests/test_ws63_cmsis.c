#include "desk_ws63_cmsis.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct desk_test_message_queue {
    uint32_t message_size;
    uint32_t capacity;
    uint32_t count;
    uint32_t last_event;
    unsigned puts;
    bool fail_next;
};

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id)
{
    return mq_id == NULL ? 0u : mq_id->message_size;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
                              uint8_t msg_prio, uint32_t timeout)
{
    assert(msg_prio == 0u && timeout == 0u);
    assert(mq_id != NULL && msg_ptr != NULL);
    mq_id->puts++;
    if (mq_id->fail_next) {
        mq_id->fail_next = false;
        return osError;
    }
    if (mq_id->count >= mq_id->capacity) return osErrorResource;
    /* Faithful copy of the 4-byte CMSIS queue message. */
    assert(mq_id->message_size == sizeof(uint32_t));
    memcpy(&mq_id->last_event, msg_ptr, sizeof(uint32_t));
    mq_id->count++;
    return osOK;
}

typedef struct {
    struct desk_test_message_queue queue;
    desk_ws63_cmsis_t adapter;
    desk_ack_t last_ack;
    unsigned ack_count;
    unsigned saved;
    bool allowed;
} fixture_t;

static int nv_read(void *ctx, desk_content_t *out, uint32_t *revision)
{
    (void)ctx; (void)out; (void)revision;
    return 0; /* empty storage/first startup */
}
static int nv_write(void *ctx, const desk_content_t *value, uint32_t revision)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(desk_content_valid(value) && revision == 1u);
    f->saved++;
    return 1;
}
static void post_event(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    desk_ws63_cmsis_notify(&f->adapter);
}
static bool auth_ui(void *ctx, desk_channel_t channel, uint32_t peer)
{
    const fixture_t *f = (const fixture_t *)ctx;
    return f->allowed && channel == DESK_CHANNEL_WIFI && peer == 27u;
}
static void send_ack(void *ctx, desk_channel_t channel, uint32_t peer,
                     const uint8_t bytes[DESK_ACK_SIZE])
{
    fixture_t *f = (fixture_t *)ctx;
    assert(channel == DESK_CHANNEL_WIFI && peer == 27u);
    assert(desk_wire_decode_ack(bytes, DESK_ACK_SIZE, &f->last_ack) ==
           DESK_WIRE_OK);
    f->ack_count++;
}
static void send_status(void *ctx, desk_channel_t channel, uint32_t peer,
                        desk_result_t result, const desk_http_state_t *snapshot)
{
    (void)ctx; (void)channel; (void)peer;
    (void)result; (void)snapshot;
}

int main(void)
{
    fixture_t f = {0};
    f.allowed = true;
    f.queue.message_size = 8u; /* wrong upstream queue item size: refuse. */
    f.queue.capacity = 1u;
    assert(!desk_ws63_cmsis_init(&f.adapter, &f.queue, 0x73u));
    assert(!desk_ws63_cmsis_post_update(&f.adapter));

    f.queue.message_size = 4u;
    assert(desk_ws63_cmsis_init(&f.adapter, &f.queue, 0x73u));
    assert(f.adapter.ready);

    desk_ws63_callbacks_t callbacks = {0};
    callbacks.load = nv_read;
    callbacks.save = nv_write;
    callbacks.post_display_event = post_event;
    callbacks.enqueue_ack = send_ack;
    callbacks.authorize_on_ui = auth_ui;
    callbacks.enqueue_status = send_status;
    callbacks.context = &f;

    desk_ws63_port_t port = {0};
    assert(desk_ws63_port_init(&port, callbacks, NULL));

    desk_dispatch_item_t command = {0};
    command.channel = DESK_CHANNEL_WIFI;
    command.peer_token = 27u;
    command.authorized = true;
    command.command.request_id = 12u;
    command.command.expected_revision = 0u;
    strcpy(command.command.content.name, "王五");
    strcpy(command.command.content.title, "开发者");
    strcpy(command.command.content.organization, "OpenHarmony");
    strcpy(command.command.content.theme, "modern");

    /* Force full CMSIS display queue; write was accepted by the internal
     * FIFO but the wakeup message cannot be queued immediately. */
    f.queue.count = 1u;
    assert(desk_ws63_port_submit(&port, &command) ==
           DESK_DISPATCH_QUEUED);
    assert(f.adapter.failed_post_count == 1u);
    assert(f.ack_count == 0u && port.core.revision == 0u);
    assert(f.queue.puts == 1u);

    /* LvglTask periodic poll drains pending work even after a failed wake.
     * Without this invariant, requests could remain stuck forever. */
    assert(desk_ws63_cmsis_ui_poll(&f.adapter, &port) == 1u);
    assert(port.core.revision == 1u);
    assert(f.saved == 1u && f.ack_count == 1u);
    assert(f.last_ack.result == DESK_OK && f.last_ack.revision == 1u);
    assert(strcmp(port.view.name_label->text, "王五") == 0);

    f.queue.count = 0u;
    assert(desk_ws63_cmsis_post_update(&f.adapter));
    assert(f.queue.count == 1u);
    assert(f.queue.last_event == 0x73u);
    assert(desk_ws63_cmsis_ui_poll(&f.adapter, &port) == 0u);
    assert(f.queue.puts == 2u);
    assert(!desk_ws63_cmsis_ui_poll(NULL, &port));
    assert(!desk_ws63_cmsis_ui_poll(&f.adapter, NULL));

    /* An OS error from queue put is visible to diagnostics. */
    f.queue.fail_next = true;
    assert(!desk_ws63_cmsis_post_update(&f.adapter));
    assert(f.adapter.failed_post_count == 2u);

    desk_ws63_port_destroy(&port);
    assert(lvgl_stub_objects_alive() == 0u);
    puts("PASS: CMSIS queue size guard, nonblocking event, full-queue recovery poll");
    return 0;
}
