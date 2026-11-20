#include "desk_reply_mailbox.h"

#include <limits.h>
#include <string.h>

static bool valid_channel(desk_channel_t channel)
{
    return channel == DESK_CHANNEL_BLE || channel == DESK_CHANNEL_SLE ||
           channel == DESK_CHANNEL_WIFI;
}
static void box_lock(desk_reply_mailbox_t *box)
{
    if (box->hooks.lock != NULL) box->hooks.lock(box->hooks.context);
}
static void box_unlock(desk_reply_mailbox_t *box)
{
    if (box->hooks.unlock != NULL) box->hooks.unlock(box->hooks.context);
}

bool desk_reply_init(desk_reply_mailbox_t *box, desk_reply_hooks_t hooks)
{
    if (box == NULL || ((hooks.lock == NULL) != (hooks.unlock == NULL)))
        return false;
    memset(box, 0, sizeof(*box));
    box->hooks = hooks;
    return true;
}

static bool push(desk_reply_mailbox_t *box, const desk_reply_item_t *item)
{
    box_lock(box);
    if (box->count >= DESK_REPLY_CAPACITY) {
        if (box->rejected_full != UINT32_MAX) box->rejected_full++;
        box_unlock(box);
        return false;
    }
    const unsigned next = (box->head + box->count) % DESK_REPLY_CAPACITY;
    box->queue[next] = *item;
    box->count++;
    box_unlock(box);
    if (box->hooks.notify_worker != NULL)
        box->hooks.notify_worker(box->hooks.context);
    return true;
}

bool desk_reply_push_ack(desk_reply_mailbox_t *box, desk_channel_t channel,
                         uint32_t peer_token, const uint8_t ack[DESK_ACK_SIZE])
{
    if (box == NULL || !valid_channel(channel) || peer_token == 0u ||
        ack == NULL) return false;
    desk_ack_t decoded = {0};
    if (desk_wire_decode_ack(ack, DESK_ACK_SIZE, &decoded) != DESK_WIRE_OK)
        return false;
    desk_reply_item_t item = {0};
    item.kind = DESK_REPLY_ACK;
    item.channel = channel;
    item.peer_token = peer_token;
    item.result = decoded.result;
    memcpy(item.payload.ack, ack, DESK_ACK_SIZE);
    return push(box, &item);
}

bool desk_reply_push_status(desk_reply_mailbox_t *box, desk_channel_t channel,
                            uint32_t peer_token, desk_result_t result,
                            const desk_http_state_t *state)
{
    if (box == NULL || !valid_channel(channel) || peer_token == 0u ||
        result < DESK_OK || result > DESK_REVISION_EXHAUSTED ||
        (result == DESK_OK && (state == NULL ||
           !desk_content_valid(&state->content) ||
           !desk_content_valid(&state->display))))
        return false;
    desk_reply_item_t item = {0};
    item.kind = DESK_REPLY_STATUS;
    item.channel = channel;
    item.peer_token = peer_token;
    item.result = result;
    /* A rejected or revoked request must never expose a stale snapshot. */
    if (result == DESK_OK) {
        item.has_state = true;
        item.payload.state = *state;
    }
    return push(box, &item);
}

bool desk_reply_pop(desk_reply_mailbox_t *box, desk_reply_item_t *out)
{
    if (box == NULL || out == NULL) return false;
    box_lock(box);
    if (box->count == 0u) {
        box_unlock(box);
        return false;
    }
    *out = box->queue[box->head];
    /* Clear potentially sensitive name/title/org data immediately on pop. */
    memset(&box->queue[box->head], 0, sizeof(box->queue[box->head]));
    box->head = (box->head + 1u) % DESK_REPLY_CAPACITY;
    box->count--;
    box_unlock(box);
    return true;
}

/*
 * Remove one logical FIFO index; caller holds the mailbox lock.
 * Moving at most CAPACITY-1 records keeps RAM and runtime bounded. This
 * preserves the relative order of every other connection's responses.
 */
static void erase_at(desk_reply_mailbox_t *box, unsigned index)
{
    for (unsigned i = index; i + 1u < box->count; ++i) {
        const unsigned here = (box->head + i) % DESK_REPLY_CAPACITY;
        const unsigned next = (box->head + i + 1u) % DESK_REPLY_CAPACITY;
        box->queue[here] = box->queue[next];
    }
    const unsigned last = (box->head + box->count - 1u) %
                          DESK_REPLY_CAPACITY;
    /* No copies of an already-consumed person's name left in the ring. */
    memset(&box->queue[last], 0, sizeof(box->queue[last]));
    box->count--;
    if (box->count == 0u) box->head = 0u;
}

bool desk_reply_take_for(desk_reply_mailbox_t *box, desk_channel_t channel,
                         uint32_t peer_token, desk_reply_item_t *out)
{
    if (box == NULL || out == NULL || !valid_channel(channel) ||
        peer_token == 0u) return false;

    box_lock(box);
    for (unsigned i = 0u; i < box->count; ++i) {
        const unsigned slot = (box->head + i) % DESK_REPLY_CAPACITY;
        if (box->queue[slot].channel == channel &&
            box->queue[slot].peer_token == peer_token) {
            *out = box->queue[slot];
            erase_at(box, i);
            box_unlock(box);
            return true;
        }
    }
    box_unlock(box);
    return false;
}

unsigned desk_reply_discard_peer(desk_reply_mailbox_t *box,
                                 desk_channel_t channel, uint32_t peer_token)
{
    if (box == NULL || !valid_channel(channel) || peer_token == 0u)
        return 0u;

    unsigned removed = 0u;
    box_lock(box);
    for (unsigned i = 0u; i < box->count;) {
        const unsigned slot = (box->head + i) % DESK_REPLY_CAPACITY;
        if (box->queue[slot].channel == channel &&
            box->queue[slot].peer_token == peer_token) {
            erase_at(box, i);
            removed++;
        } else {
            i++;
        }
    }
    box_unlock(box);
    return removed;
}

unsigned desk_reply_pending(desk_reply_mailbox_t *box)
{
    if (box == NULL) return 0u;
    box_lock(box);
    const unsigned count = box->count;
    box_unlock(box);
    return count;
}

uint32_t desk_reply_rejected_full(desk_reply_mailbox_t *box)
{
    if (box == NULL) return 0u;
    box_lock(box);
    const uint32_t count = box->rejected_full;
    box_unlock(box);
    return count;
}
