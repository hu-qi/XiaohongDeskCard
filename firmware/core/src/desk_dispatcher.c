#include "desk_dispatcher.h"
#include <string.h>

static bool valid_channel(desk_channel_t channel)
{
    return channel == DESK_CHANNEL_BLE || channel == DESK_CHANNEL_SLE ||
           channel == DESK_CHANNEL_WIFI;
}

static void queue_lock(desk_dispatcher_t *dispatcher)
{
    if (dispatcher->hooks.lock != NULL)
        dispatcher->hooks.lock(dispatcher->hooks.context);
}

static void queue_unlock(desk_dispatcher_t *dispatcher)
{
    if (dispatcher->hooks.unlock != NULL)
        dispatcher->hooks.unlock(dispatcher->hooks.context);
}

bool desk_dispatcher_init(desk_dispatcher_t *dispatcher, desk_core_t *core,
                          desk_dispatch_hooks_t hooks)
{
    if (dispatcher == NULL || core == NULL ||
        ((hooks.lock == NULL) != (hooks.unlock == NULL))) return false;
    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->core = core;
    dispatcher->hooks = hooks;
    return true;
}

desk_dispatch_status_t desk_dispatcher_submit(desk_dispatcher_t *dispatcher,
                                              const desk_dispatch_item_t *item)
{
    if (dispatcher == NULL || dispatcher->core == NULL || item == NULL ||
        !valid_channel(item->channel) || item->peer_token == 0u ||
        item->command.request_id == 0u)
        return DESK_DISPATCH_INVALID;

    queue_lock(dispatcher);
    if (dispatcher->count >= DESK_DISPATCH_QUEUE_CAPACITY) {
        queue_unlock(dispatcher);
        return DESK_DISPATCH_BUSY;
    }

    const unsigned tail =
        (dispatcher->head + dispatcher->count) % DESK_DISPATCH_QUEUE_CAPACITY;
    dispatcher->queue[tail] = *item;
    dispatcher->count++;
    queue_unlock(dispatcher);

    /* Wake after unlocking; the UI worker will do validation and rendering. */
    if (dispatcher->hooks.notify_ui != NULL)
        dispatcher->hooks.notify_ui(dispatcher->hooks.context);
    return DESK_DISPATCH_QUEUED;
}

bool desk_dispatcher_process_one(desk_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL || dispatcher->core == NULL) return false;

    queue_lock(dispatcher);
    if (dispatcher->count == 0u) {
        queue_unlock(dispatcher);
        return false;
    }
    const desk_dispatch_item_t item = dispatcher->queue[dispatcher->head];
    dispatcher->head = (dispatcher->head + 1u) % DESK_DISPATCH_QUEUE_CAPACITY;
    dispatcher->count--;
    queue_unlock(dispatcher);

    bool currently_authorized = item.authorized;
    if (currently_authorized && dispatcher->hooks.authorize_on_ui != NULL) {
        currently_authorized = dispatcher->hooks.authorize_on_ui(
            dispatcher->hooks.context, item.channel, item.peer_token);
    }
    const desk_result_t result =
        desk_core_apply(dispatcher->core, &item.command, currently_authorized);
    if (dispatcher->hooks.on_result != NULL) {
        const desk_ack_t ack = {
            item.command.request_id,
            dispatcher->core->revision,
            result
        };
        uint8_t encoded[DESK_ACK_SIZE];
        if (desk_wire_encode_ack(&ack, encoded, sizeof(encoded)) == DESK_WIRE_OK)
            dispatcher->hooks.on_result(dispatcher->hooks.context, item.channel,
                                        item.peer_token, encoded);
    }
    return true;
}

unsigned desk_dispatcher_pending(desk_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL || dispatcher->core == NULL) return 0u;
    queue_lock(dispatcher);
    const unsigned count = dispatcher->count;
    queue_unlock(dispatcher);
    return count;
}
