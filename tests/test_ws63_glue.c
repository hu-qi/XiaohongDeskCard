#include "desk_ws63_glue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned wakes;
    unsigned locks;
    unsigned unlocks;
    unsigned saves;
    unsigned acks;
    unsigned statuses;
    desk_result_t last_status_result;
    desk_http_state_t last_status_state;
    bool locked;
    bool currently_authorized;
    desk_content_t content;
    uint32_t revision;
    desk_ack_t last_ack;
} fake_platform_t;

static int load_nv(void *ctx, desk_content_t *out, uint32_t *rev)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    if (p->revision == 0u) return 0;
    *out = p->content;
    *rev = p->revision;
    return 1;
}
static int save_nv(void *ctx, const desk_content_t *content, uint32_t rev)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    assert(!p->locked);
    p->content = *content;
    p->revision = rev;
    p->saves++;
    return 1;
}
static void notify(void *ctx)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    assert(!p->locked);
    p->wakes++;
}
static void ack(void *ctx, desk_channel_t channel, uint32_t peer,
                const uint8_t bytes[DESK_ACK_SIZE])
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    assert(channel == DESK_CHANNEL_BLE && peer == 55u);
    assert(desk_wire_decode_ack(bytes, DESK_ACK_SIZE, &p->last_ack) == DESK_WIRE_OK);
    p->acks++;
}
static void on_status(void *ctx, desk_channel_t channel, uint32_t peer,
                      desk_result_t result, const desk_http_state_t *snapshot)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    assert(channel == DESK_CHANNEL_BLE && peer == 55u);
    p->statuses++;
    p->last_status_result = result;
    if (result == DESK_OK) {
        assert(snapshot != NULL);
        p->last_status_state = *snapshot;
    } else {
        assert(snapshot == NULL);
    }
}

static void lock(void *ctx)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    assert(!p->locked);
    p->locked = true;
    p->locks++;
}
static void unlock(void *ctx)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    assert(p->locked);
    p->locked = false;
    p->unlocks++;
}

static bool authorize_on_ui(void *ctx, desk_channel_t channel, uint32_t peer)
{
    fake_platform_t *p = (fake_platform_t *)ctx;
    return p->currently_authorized && channel == DESK_CHANNEL_BLE && peer == 55u;
}

int main(void)
{
    desk_ws63_port_t port = {0};
    fake_platform_t platform = {0};
    platform.currently_authorized = true;
    desk_dispatch_item_t item = {0};
    assert(desk_ws63_port_submit(&port, &item) == DESK_DISPATCH_INVALID);
    assert(desk_ws63_port_ui_drain(&port) == 0u);

    desk_ws63_callbacks_t callbacks = {load_nv, save_nv, notify, ack, lock, unlock,
                                       &platform, authorize_on_ui, on_status};
    desk_ws63_callbacks_t missing_status = callbacks;
    missing_status.enqueue_status = NULL;
    assert(!desk_ws63_port_init(&port, missing_status, NULL));
    desk_ws63_callbacks_t missing_recheck = callbacks;
    missing_recheck.authorize_on_ui = NULL;
    assert(!desk_ws63_port_init(&port, missing_recheck, NULL));
    assert(desk_ws63_port_init(&port, callbacks, NULL));
    assert(lvgl_stub_objects_alive() == 6u);
    assert(strcmp(port.view.name_label->text, "Xiaohong") == 0);

    item.channel = DESK_CHANNEL_BLE;
    item.peer_token = 55u;
    item.authorized = true;
    item.command.request_id = 1u;
    item.command.expected_revision = 0u;
    strcpy(item.command.content.name, "胡琦");
    strcpy(item.command.content.title, "桌牌作者");
    strcpy(item.command.content.organization, "OpenHarmony");
    strcpy(item.command.content.theme, "modern");

    assert(desk_ws63_port_submit(&port, &item) == DESK_DISPATCH_QUEUED);
    assert(platform.wakes == 1u && platform.acks == 0u);
    assert(strcmp(port.view.name_label->text, "Xiaohong") == 0);
    assert(desk_ws63_port_ui_drain(&port) == 1u);
    assert(platform.acks == 1u && platform.saves == 1u);
    assert(port.core.revision == 1u);
    assert(platform.last_ack.request_id == 1u);
    assert(platform.last_ack.result == DESK_OK);
    assert(strcmp(port.view.name_label->text, "胡琦") == 0);
    assert(port.view.header->bg == 0x173A60u);
    assert(platform.locks == platform.unlocks && !platform.locked);

    /* BLE status shares the exact UI FIFO and reflects the applied content. */
    assert(desk_dispatcher_submit_status(&port.dispatcher, DESK_CHANNEL_BLE,
                                         55u, true) == DESK_DISPATCH_QUEUED);
    assert(desk_ws63_port_ui_drain(&port) == 1u);
    assert(platform.statuses == 1u);
    assert(platform.last_status_result == DESK_OK);
    assert(platform.last_status_state.revision == 1u);
    assert(strcmp(platform.last_status_state.display.name, "胡琦") == 0);

    /* A command accepted by BLE before owner revocation must be rejected by
     * the UI task immediately before applying NV/display changes. */
    item.command.request_id = 2u;
    item.command.expected_revision = 1u;
    assert(desk_ws63_port_submit(&port, &item) == DESK_DISPATCH_QUEUED);
    platform.currently_authorized = false;
    assert(desk_ws63_port_ui_drain(&port) == 1u);
    assert(platform.last_ack.request_id == 2u);
    assert(platform.last_ack.result == DESK_UNAUTHORIZED);
    assert(platform.saves == 1u && port.core.revision == 1u);
    /* Queued status is equally revoked at execution time: never leak name. */
    assert(desk_dispatcher_submit_status(&port.dispatcher, DESK_CHANNEL_BLE,
                                         55u, true) == DESK_DISPATCH_QUEUED);
    assert(desk_ws63_port_ui_drain(&port) == 1u);
    assert(platform.statuses == 2u);
    assert(platform.last_status_result == DESK_UNAUTHORIZED);
    platform.currently_authorized = true;

    desk_ws63_port_destroy(&port);
    assert(lvgl_stub_objects_alive() == 0u);

    /* Simulated restart: persisted content is restored by the storage hook. */
    assert(desk_ws63_port_init(&port, callbacks, NULL));
    assert(port.core.revision == 1u);
    assert(strcmp(port.view.name_label->text, "胡琦") == 0);
    desk_ws63_port_destroy(&port);
    assert(lvgl_stub_objects_alive() == 0u);

    puts("PASS: WS63 glue (mock platform): task boundary, NV, LVGL, ACK, restart");
    return 0;
}
