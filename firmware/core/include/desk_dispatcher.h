#ifndef XIAOHONG_DESK_DISPATCHER_H
#define XIAOHONG_DESK_DISPATCHER_H

#include "desk_core.h"
#include "desk_wire.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * One bounded, allocation-free multi-producer queue feeding a SINGLE UI task.
 * Peer identity is a transport-owned, short-lived connection token, never a
 * device address. The caller must authorize ownership before accepting writes.
 */
#define DESK_DISPATCH_QUEUE_CAPACITY 4u

typedef enum {
    DESK_CHANNEL_BLE = 1,
    DESK_CHANNEL_SLE = 2,
    DESK_CHANNEL_WIFI = 3
} desk_channel_t;

typedef enum {
    DESK_DISPATCH_QUEUED = 0,
    DESK_DISPATCH_BUSY = 1,
    DESK_DISPATCH_INVALID = 2
} desk_dispatch_status_t;

typedef struct {
    desk_channel_t channel;
    uint32_t peer_token;
    bool authorized;
    desk_set_command_t command;
} desk_dispatch_item_t;

typedef struct {
    /*
     * lock/unlock must be supplied as a pair for concurrent radio/HTTP tasks.
     * Must be short, non-allocating and safe in the caller's task context.
     * Do not invoke submit() from an ISR.
     */
    void (*lock)(void *context);
    void (*unlock)(void *context);
    /* Wake the LVGL owner task by posting a display event. */
    void (*notify_ui)(void *context);
    /*
     * Executed on the UI owner task AFTER desk_core_apply().
     * This callback should enqueue ACK delivery to a transport worker,
     * NOT block waiting for wireless I/O.
     */
    void (*on_result)(void *context, desk_channel_t channel,
                      uint32_t peer_token, const uint8_t ack[DESK_ACK_SIZE]);
    void *context;
    /*
     * Revalidate current connection generation + device ownership when the
     * UI task ACTUALLY processes the command, not only at radio ingress.
     * Mandatory for production devices; optional for legacy host tests.
     */
    bool (*authorize_on_ui)(void *context, desk_channel_t channel,
                            uint32_t peer_token);
} desk_dispatch_hooks_t;

typedef struct {
    desk_dispatch_item_t queue[DESK_DISPATCH_QUEUE_CAPACITY];
    unsigned head;
    unsigned count;
    desk_core_t *core;
    desk_dispatch_hooks_t hooks;
} desk_dispatcher_t;

bool desk_dispatcher_init(desk_dispatcher_t *dispatcher, desk_core_t *core,
                          desk_dispatch_hooks_t hooks);

/* May be called on BLE/SLE/HTTP worker tasks, but not interrupt context. */
desk_dispatch_status_t desk_dispatcher_submit(desk_dispatcher_t *dispatcher,
                                              const desk_dispatch_item_t *item);

/*
 * Only call from the single LVGL owner task.
 * Returns false if queue empty, true if one queued request was processed.
 */
bool desk_dispatcher_process_one(desk_dispatcher_t *dispatcher);

unsigned desk_dispatcher_pending(desk_dispatcher_t *dispatcher);

#endif
