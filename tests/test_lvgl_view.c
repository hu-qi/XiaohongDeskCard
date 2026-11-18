#include "desk_view_lvgl.h"
#include "desk_dispatcher.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned callback_count;
    desk_ack_t last_ack;
} test_ack_t;

static void on_ack(void *context, desk_channel_t channel,
                   uint32_t peer_token, const uint8_t ack[DESK_ACK_SIZE])
{
    test_ack_t *state = (test_ack_t *)context;
    assert(channel == DESK_CHANNEL_SLE && peer_token == 100u);
    assert(desk_wire_decode_ack(ack, DESK_ACK_SIZE, &state->last_ack) == DESK_WIRE_OK);
    state->callback_count++;
}

int main(void)
{
    desk_lvgl_view_t view = {0};
    assert(!desk_lvgl_view_render(&view, NULL));
    assert(desk_lvgl_view_create(&view, NULL));
    assert(view.root != NULL && view.root->w == 240 && view.root->h == 240);
    assert(view.header != NULL && view.header->h == 42);
    assert(view.name_label->y == 91 && view.title_label->y == 153);
    assert(lvgl_stub_objects_alive() == 6u);

    desk_core_t core;
    desk_hooks_t hooks = {NULL, NULL, desk_lvgl_view_render, &view};
    desk_core_init(&core, hooks);
    assert(desk_core_repaint(&core) == DESK_OK);
    assert(strcmp(view.name_label->text, "Xiaohong") == 0);

    test_ack_t ack = {0};
    desk_dispatch_hooks_t dispatcher_hooks = {NULL, NULL, NULL, on_ack, &ack};
    desk_dispatcher_t dispatcher;
    assert(desk_dispatcher_init(&dispatcher, &core, dispatcher_hooks));

    /* SLE worker queues a command, then owner task renders it to LVGL. */
    desk_dispatch_item_t item = {0};
    item.channel = DESK_CHANNEL_SLE;
    item.peer_token = 100u;
    item.authorized = true;
    item.command.request_id = 7u;
    item.command.expected_revision = 0u;
    strcpy(item.command.content.name, "胡琦");
    strcpy(item.command.content.title, "技术负责人");
    strcpy(item.command.content.organization, "OpenHarmony 社区");
    strcpy(item.command.content.theme, "classic");
    assert(desk_dispatcher_submit(&dispatcher, &item) == DESK_DISPATCH_QUEUED);
    /* No UI changes in transport worker callback. */
    assert(strcmp(view.name_label->text, "Xiaohong") == 0);
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(strcmp(view.name_label->text, "胡琦") == 0);
    assert(strcmp(view.org_label->text, "OpenHarmony 社区") == 0);
    assert(strcmp(view.title_label->text, "技术负责人") == 0);
    assert(view.header->bg == 0xB32942u);
    assert(ack.callback_count == 1u && ack.last_ack.revision == 1u);
    assert(ack.last_ack.result == DESK_OK);

    /* Theme switching is pure UI update; same label objects are reused. */
    desk_content_t modern = core.content;
    strcpy(modern.theme, "modern");
    assert(desk_lvgl_view_render(&view, &modern));
    assert(view.header->bg == 0x173A60u);
    strcpy(modern.theme, "minimal");
    assert(desk_lvgl_view_render(&view, &modern));
    assert(view.header->bg == 0xFFFFFFu);
    assert(view.org_label->fg == 0xB32942u);
    assert(!desk_lvgl_view_render(NULL, &modern));

    desk_lvgl_view_destroy(&view);
    assert(lvgl_stub_objects_alive() == 0u);
    assert(view.root == NULL);
    puts("PASS: LVGL 240x240 objects, UTF-8 labels, themes, Desk Core dispatcher -> ACK");
    return 0;
}
