#ifndef XIAOHONG_DESK_REPLY_MAILBOX_H
#define XIAOHONG_DESK_REPLY_MAILBOX_H

#include "desk_dispatcher.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Fixed-size cross-task handoff for results emitted by LVGL owner task.
 * Radio and HTTP worker tasks consume replies and compare channel+peer_token
 * against their live connection generation before any network transmission.
 *
 * This is an in-RAM mailbox, NOT a persistent command or security journal.
 * ACK delivery may be lost on disconnect/full queue: treat unknown outcome
 * as UNKNOWN and reconcile current status; NEVER report successful delivery.
 */
#define DESK_REPLY_CAPACITY 4u

typedef enum {
    DESK_REPLY_ACK = 1,
    DESK_REPLY_STATUS = 2
} desk_reply_kind_t;

typedef struct {
    desk_reply_kind_t kind;
    desk_channel_t channel;
    uint32_t peer_token;
    desk_result_t result; /* Status error; use ack to decode write outcome. */
    bool has_state;
    union {
        uint8_t ack[DESK_ACK_SIZE];
        desk_http_state_t state;
    } payload;
} desk_reply_item_t;

typedef struct {
    /* Lock/unlock must be supplied as a pair for true multi-task use. */
    void (*lock)(void *context);
    void (*unlock)(void *context);
    /* NONBLOCKING wake of the communications worker; never socket write. */
    void (*notify_worker)(void *context);
    void *context;
} desk_reply_hooks_t;

typedef struct {
    desk_reply_item_t queue[DESK_REPLY_CAPACITY];
    unsigned head;
    unsigned count;
    uint32_t rejected_full; /* Diagnostic count, saturates at UINT32_MAX. */
    desk_reply_hooks_t hooks;
} desk_reply_mailbox_t;

bool desk_reply_init(desk_reply_mailbox_t *box, desk_reply_hooks_t hooks);

/* Only call on LVGL owner task or another producer under the box mutex. */
bool desk_reply_push_ack(desk_reply_mailbox_t *box, desk_channel_t channel,
                         uint32_t peer_token, const uint8_t ack[DESK_ACK_SIZE]);

/* state required only when result=DESK_OK. Unauthorized status leaks NOTHING. */
bool desk_reply_push_status(desk_reply_mailbox_t *box, desk_channel_t channel,
                            uint32_t peer_token, desk_result_t result,
                            const desk_http_state_t *state);

/* Only call on consuming communications task. Copies the entire result. */
bool desk_reply_pop(desk_reply_mailbox_t *box, desk_reply_item_t *out);
unsigned desk_reply_pending(desk_reply_mailbox_t *box);
uint32_t desk_reply_rejected_full(desk_reply_mailbox_t *box);

#endif
