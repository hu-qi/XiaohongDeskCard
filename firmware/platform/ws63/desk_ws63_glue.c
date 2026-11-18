#include "desk_ws63_glue.h"
#include <string.h>

static int port_load(void *ctx, desk_content_t *out, uint32_t *revision)
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    return port->callbacks.load(port->callbacks.context, out, revision);
}

static int port_save(void *ctx, const desk_content_t *value, uint32_t revision)
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    return port->callbacks.save(port->callbacks.context, value, revision);
}

static int port_render(void *ctx, const desk_content_t *value)
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    return desk_lvgl_view_render(&port->view, value);
}

static void port_lock(void *ctx)
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    port->callbacks.lock(port->callbacks.context);
}

static void port_unlock(void *ctx)
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    port->callbacks.unlock(port->callbacks.context);
}

static void port_notify(void *ctx)
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    port->callbacks.post_display_event(port->callbacks.context);
}

static void port_ack(void *ctx, desk_channel_t channel, uint32_t peer,
                     const uint8_t ack[DESK_ACK_SIZE])
{
    desk_ws63_port_t *port = (desk_ws63_port_t *)ctx;
    port->callbacks.enqueue_ack(port->callbacks.context, channel, peer, ack);
}

int desk_ws63_port_init(desk_ws63_port_t *port, desk_ws63_callbacks_t callbacks,
                        lv_obj_t *parent)
{
    if (port == NULL || callbacks.load == NULL || callbacks.save == NULL ||
        callbacks.post_display_event == NULL || callbacks.enqueue_ack == NULL ||
        ((callbacks.lock == NULL) != (callbacks.unlock == NULL)))
        return 0;
    memset(port, 0, sizeof(*port));
    port->callbacks = callbacks;
    if (!desk_lvgl_view_create(&port->view, parent)) return 0;
    const desk_hooks_t core_hooks = {port_load, port_save, port_render, port};
    desk_core_init(&port->core, core_hooks);
    if (desk_core_repaint(&port->core) != DESK_OK) {
        desk_lvgl_view_destroy(&port->view);
        return 0;
    }
    desk_dispatch_hooks_t dispatch_hooks = {0};
    if (callbacks.lock != NULL) {
        dispatch_hooks.lock = port_lock;
        dispatch_hooks.unlock = port_unlock;
    }
    dispatch_hooks.notify_ui = port_notify;
    dispatch_hooks.on_result = port_ack;
    dispatch_hooks.context = port;
    if (!desk_dispatcher_init(&port->dispatcher, &port->core, dispatch_hooks)) {
        desk_lvgl_view_destroy(&port->view);
        return 0;
    }
    port->ready = true;
    return 1;
}

desk_dispatch_status_t desk_ws63_port_submit(desk_ws63_port_t *port,
                                              const desk_dispatch_item_t *item)
{
    if (port == NULL || !port->ready) return DESK_DISPATCH_INVALID;
    return desk_dispatcher_submit(&port->dispatcher, item);
}

unsigned desk_ws63_port_ui_drain(desk_ws63_port_t *port)
{
    if (port == NULL || !port->ready) return 0u;
    unsigned processed = 0u;
    while (processed < DESK_DISPATCH_QUEUE_CAPACITY &&
           desk_dispatcher_process_one(&port->dispatcher))
        processed++;
    return processed;
}

void desk_ws63_port_destroy(desk_ws63_port_t *port)
{
    if (port == NULL || !port->ready) return;
    port->ready = false;
    desk_lvgl_view_destroy(&port->view);
    memset(&port->dispatcher, 0, sizeof(port->dispatcher));
}
