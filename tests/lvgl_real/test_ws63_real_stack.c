/*
 * Actual LVGL 8/9 + ARM CMSIS-RTOS2 public API + all 12 DeskCard C sources.
 * Board stubs are intentionally limited to simulated NV, event queue and radio.
 * No assertion of WS63 SDK linking, ST7789 SPI or radio hardware capability.
 */
#include "desk_ws63_cmsis.h"
#include "desk_reply_mailbox.h"
#include "desk_wifi_ingress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t msg_size;
    unsigned count;
    unsigned capacity;
    unsigned post_errors;
    uint32_t last_message;
} fake_queue_t;

typedef struct {
    fake_queue_t events;
    desk_ws63_cmsis_t cmsis;
    desk_reply_mailbox_t replies;
    desk_content_t saved;
    uint32_t revision;
    unsigned saved_count;
    bool authorized;
} fixture_t;

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id)
{
    const fake_queue_t *q = (const fake_queue_t *)mq_id;
    return q->msg_size;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg,
                              uint8_t prio, uint32_t timeout)
{
    fake_queue_t *q = (fake_queue_t *)mq_id;
    assert(msg != NULL && prio == 0u && timeout == 0u);
    if (q->count >= q->capacity) {
        q->post_errors++;
        return osErrorResource;
    }
    assert(q->msg_size == sizeof(uint32_t));
    memcpy(&q->last_message, msg, sizeof(uint32_t));
    q->count++;
    return osOK;
}

#if LVGL_VERSION_MAJOR >= 9
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}
#else
static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    (void)area;
    (void)colors;
    lv_disp_flush_ready(drv);
}
#endif

static int nv_load(void *ctx, desk_content_t *out, uint32_t *rev)
{
    fixture_t *f = (fixture_t *)ctx;
    if (!f->saved_count) return 0;
    *out = f->saved;
    *rev = f->revision;
    return 1;
}

static int nv_save(void *ctx, const desk_content_t *content, uint32_t revision)
{
    fixture_t *f = (fixture_t *)ctx;
    f->saved = *content;
    f->revision = revision;
    f->saved_count++;
    return 1;
}

static bool authorization(void *ctx, desk_channel_t channel, uint32_t token)
{
    fixture_t *f = (fixture_t *)ctx;
    return f->authorized && channel == DESK_CHANNEL_WIFI && token == 73u;
}

static void notify_ui(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    desk_ws63_cmsis_notify(&f->cmsis);
}

static void send_ack(void *ctx, desk_channel_t channel, uint32_t token,
                     const uint8_t bytes[DESK_ACK_SIZE])
{
    fixture_t *f = (fixture_t *)ctx;
    assert(desk_reply_push_ack(&f->replies, channel, token, bytes));
}

static void send_status(void *ctx, desk_channel_t channel, uint32_t token,
                        desk_result_t result, const desk_http_state_t *state)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(desk_reply_push_status(&f->replies, channel, token, result, state));
}

int main(void)
{
    lv_init();
#if LVGL_VERSION_MAJOR >= 9
    static uint8_t pixels[240u * 8u * 2u];
    lv_display_t *display = lv_display_create(240, 240);
    assert(display != NULL);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_t *screen = lv_screen_active();
#else
    static lv_color_t pixels[240u * 8u];
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, pixels, NULL, 240u * 8u);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 240;
    driver.ver_res = 240;
    driver.draw_buf = &draw;
    driver.flush_cb = flush;
    assert(lv_disp_drv_register(&driver) != NULL);
    lv_obj_t *screen = lv_scr_act();
#endif

    fixture_t f = {0};
    f.authorized = true;
    f.events.msg_size = sizeof(uint32_t);
    f.events.capacity = 1u;
    assert(desk_ws63_cmsis_init(
        &f.cmsis, (osMessageQueueId_t)&f.events, 0xD35u));
    desk_reply_hooks_t hooks = {0};
    assert(desk_reply_init(&f.replies, hooks));

    desk_ws63_callbacks_t callbacks = {0};
    callbacks.context = &f;
    callbacks.load = nv_load;
    callbacks.save = nv_save;
    callbacks.post_display_event = notify_ui;
    callbacks.enqueue_ack = send_ack;
    callbacks.enqueue_status = send_status;
    callbacks.authorize_on_ui = authorization;

    desk_ws63_port_t port = {0};
    assert(desk_ws63_port_init(&port, callbacks, screen));
    assert(strcmp(lv_label_get_text(port.view.name_label), "Xiaohong") == 0);

    desk_dispatch_item_t cmd = {0};
    cmd.channel = DESK_CHANNEL_WIFI;
    cmd.peer_token = 73u;
    cmd.authorized = true;
    cmd.command.request_id = 2026u;
    cmd.command.expected_revision = 0u;
    strcpy(cmd.command.content.name, "张三");
    strcpy(cmd.command.content.title, "技术负责人");
    strcpy(cmd.command.content.organization, "OpenHarmony 社区");
    strcpy(cmd.command.content.theme, "modern");

    assert(desk_ws63_port_submit(&port, &cmd) == DESK_DISPATCH_QUEUED);
    assert(f.events.count == 1u && f.events.last_message == 0xD35u);
    /* Consume event on the owner task, not in a transport callback. */
    f.events.count = 0u;
    assert(desk_ws63_cmsis_ui_poll(&f.cmsis, &port) == 1u);
    assert(port.core.revision == 1u && f.revision == 1u);
    assert(strcmp(lv_label_get_text(port.view.name_label), "张三") == 0);
    assert(strcmp(lv_label_get_text(port.view.title_label),
                  "技术负责人") == 0);

    desk_reply_item_t reply = {0};
    assert(desk_reply_take_for(
        &f.replies, DESK_CHANNEL_WIFI, 73u, &reply));
    assert(reply.kind == DESK_REPLY_ACK);
    desk_ack_t ack = {0};
    assert(desk_wifi_match_ack(2026u, reply.payload.ack,
                               DESK_ACK_SIZE, &ack));
    assert(ack.result == DESK_OK && ack.revision == 1u);

    assert(desk_dispatcher_submit_status(
        &port.dispatcher, DESK_CHANNEL_WIFI, 73u, true) ==
        DESK_DISPATCH_QUEUED);
    /* CMSIS event ring already full: periodic UI polling must recover. */
    assert(f.events.count == 1u);
    assert(f.cmsis.failed_post_count == 0u);
    assert(desk_ws63_cmsis_ui_poll(&f.cmsis, &port) == 1u);
    assert(desk_reply_take_for(
        &f.replies, DESK_CHANNEL_WIFI, 73u, &reply));
    assert(reply.kind == DESK_REPLY_STATUS && reply.has_state);
    assert(reply.payload.state.revision == 1u);
    assert(strcmp(reply.payload.state.display.name, "张三") == 0);

    /* A revoked owner cannot read a previously queued status. */
    assert(desk_dispatcher_submit_status(
        &port.dispatcher, DESK_CHANNEL_WIFI, 73u, true) ==
        DESK_DISPATCH_QUEUED);
    f.authorized = false;
    assert(desk_ws63_cmsis_ui_poll(&f.cmsis, &port) == 1u);
    assert(desk_reply_take_for(
        &f.replies, DESK_CHANNEL_WIFI, 73u, &reply));
    assert(reply.result == DESK_UNAUTHORIZED && !reply.has_state);

    lv_timer_handler();
    desk_ws63_port_destroy(&port);
    printf("PASS: real LVGL %d.%d.%d, official CMSIS API, 12 C sources, "
           "UI/NV/status/ACK integration\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return 0;
}
