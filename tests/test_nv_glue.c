#include "desk_ws63_glue.h"
#include "desk_nv_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t flash[2][256];
    desk_nv_store_t nv;
    unsigned notified;
    unsigned acknowledgements;
    bool authorized;
    desk_ack_t last_ack;
} rig_t;

static int f_read(void *ctx, unsigned slot, size_t pos, uint8_t *out, size_t n)
{
    rig_t *rig = (rig_t *)ctx;
    if (slot >= 2u || pos > 256u || n > 256u - pos) return 0;
    memcpy(out, rig->flash[slot] + pos, n);
    return 1;
}

static int f_erase(void *ctx, unsigned slot)
{
    rig_t *rig = (rig_t *)ctx;
    if (slot >= 2u) return 0;
    memset(rig->flash[slot], 0xFF, 256);
    return 1;
}

static int f_program(void *ctx, unsigned slot, size_t pos,
                     const uint8_t *bytes, size_t n)
{
    rig_t *rig = (rig_t *)ctx;
    if (slot >= 2u || pos > 256u || n > 256u - pos ||
        pos % 4u != 0u || n % 4u != 0u) return 0;
    for (size_t i = 0; i < n; i++) {
        if ((rig->flash[slot][pos + i] & bytes[i]) != bytes[i]) return 0;
        rig->flash[slot][pos + i] &= bytes[i];
    }
    return 1;
}

static int persist(void *ctx, const desk_content_t *value, uint32_t revision)
{
    rig_t *rig = (rig_t *)ctx;
    return desk_nv_save(&rig->nv, value, revision);
}

static int restore(void *ctx, desk_content_t *value, uint32_t *revision)
{
    rig_t *rig = (rig_t *)ctx;
    return desk_nv_load(&rig->nv, value, revision);
}

static void wake(void *ctx)
{
    rig_t *rig = (rig_t *)ctx;
    rig->notified++;
}

static void ack(void *ctx, desk_channel_t channel, uint32_t peer,
                const uint8_t data[DESK_ACK_SIZE])
{
    rig_t *rig = (rig_t *)ctx;
    assert(channel == DESK_CHANNEL_SLE && peer == 77u);
    assert(desk_wire_decode_ack(data, DESK_ACK_SIZE, &rig->last_ack) ==
           DESK_WIRE_OK);
    rig->acknowledgements++;
}

static bool check_auth(void *ctx, desk_channel_t channel, uint32_t peer)
{
    rig_t *rig = (rig_t *)ctx;
    return rig->authorized && channel == DESK_CHANNEL_SLE && peer == 77u;
}

int main(void)
{
    rig_t rig = {0};
    memset(rig.flash, 0xFF, sizeof(rig.flash));
    const desk_nv_io_t io = {f_read, f_erase, f_program, &rig};
    assert(desk_nv_init(&rig.nv, io));
    rig.authorized = true;

    desk_ws63_callbacks_t cb = {0};
    cb.load = restore;
    cb.save = persist;
    cb.post_display_event = wake;
    cb.enqueue_ack = ack;
    cb.context = &rig;
    cb.authorize_on_ui = check_auth;

    desk_ws63_port_t port = {0};
    assert(desk_ws63_port_init(&port, cb, NULL));

    desk_dispatch_item_t item = {0};
    item.channel = DESK_CHANNEL_SLE;
    item.peer_token = 77u;
    item.authorized = true;
    item.command.request_id = 10u;
    item.command.expected_revision = 0u;
    strcpy(item.command.content.name, "张三");
    strcpy(item.command.content.title, "技术负责人");
    strcpy(item.command.content.organization, "OpenHarmony");
    strcpy(item.command.content.theme, "modern");

    assert(desk_ws63_port_submit(&port, &item) == DESK_DISPATCH_QUEUED);
    assert(rig.notified == 1u && rig.acknowledgements == 0u);
    assert(desk_ws63_port_ui_drain(&port) == 1u);
    assert(port.core.revision == 1u);
    assert(rig.last_ack.result == DESK_OK);
    assert(rig.acknowledgements == 1u);
    assert(strcmp(port.view.name_label->text, "张三") == 0);
    desk_ws63_port_destroy(&port);
    assert(lvgl_stub_objects_alive() == 0u);

    /* Simulate cold boot: create an entirely new NV state from same Flash. */
    memset(&rig.nv, 0, sizeof(rig.nv));
    assert(desk_nv_init(&rig.nv, io));
    assert(desk_ws63_port_init(&port, cb, NULL));
    assert(port.core.revision == 1u);
    assert(strcmp(port.view.name_label->text, "张三") == 0);
    assert(strcmp(port.view.title_label->text, "技术负责人") == 0);

    /* Queued command then owner revoked: no NV write or revision advance. */
    item.command.request_id = 11u;
    item.command.expected_revision = 1u;
    strcpy(item.command.content.name, "李四");
    assert(desk_ws63_port_submit(&port, &item) == DESK_DISPATCH_QUEUED);
    rig.authorized = false;
    assert(desk_ws63_port_ui_drain(&port) == 1u);
    assert(rig.last_ack.result == DESK_UNAUTHORIZED);
    assert(port.core.revision == 1u);
    assert(strcmp(port.view.name_label->text, "张三") == 0);
    desk_ws63_port_destroy(&port);

    desk_nv_store_t reloaded = {0};
    assert(desk_nv_init(&reloaded, io));
    desk_content_t last = {0};
    uint32_t revision = 0u;
    assert(desk_nv_load(&reloaded, &last, &revision));
    assert(revision == 1u && strcmp(last.name, "张三") == 0);
    puts("PASS: SLE queue->NV 2-slot->LVGL->ACK; cold-boot restore; revoked peer");
    return 0;
}
