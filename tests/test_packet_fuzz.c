/*
 * Deterministic invalid-radio-input regression checks.
 * This is NOT an exhaustive fuzzer. Sanitizer CI makes corruption more visible
 * before packet bytes ever touch the WS63 wireless stack.
 */
#include "desk_packet.h"
#include "desk_radio_ingress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t seed = UINT32_C(0xC0FFEE42);
static uint32_t next_random(void)
{
    uint32_t x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    seed = x;
    return x;
}

static void fill_bytes(uint8_t *bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        bytes[i] = (uint8_t)next_random();
}

int main(void)
{
    enum { MAX_TEST_PACKET = DESK_WIRE_MAX_FRAME + 24u };
    uint8_t packet[MAX_TEST_PACKET] = {0};
    desk_packet_session_t session = {0};
    desk_set_command_t output = {0};
    uint32_t now = 0u;
    unsigned accepted = 0u;

    for (unsigned i = 0u; i < 30000u; ++i) {
        const size_t size = 1u + next_random() % MAX_TEST_PACKET;
        fill_bytes(packet, size);
        /* Include syntactically plausible headers and malformed short writes. */
        if (i % 3u == 0u) packet[0] = DESK_PACKET_BEGIN;
        if (i % 3u == 1u) packet[0] = DESK_PACKET_CHUNK;
        if (i % 3u == 2u) packet[0] = DESK_PACKET_COMMIT;

        output.request_id = UINT32_C(0xABCDEF01);
        now += next_random() % 400u;
        const desk_packet_result_t result =
            desk_packet_feed(&session, packet, size, now, 600u, &output);
        assert(result >= DESK_PACKET_PROGRESS && result <= DESK_PACKET_BAD_FRAME);
        if (result == DESK_PACKET_READY) {
            /* A full random frame is extraordinarily unlikely; if one
             * decodes, it must meet all normal content invariants. */
            assert(desk_content_valid(&output.content));
            assert(output.request_id != 0u);
            ++accepted;
        } else {
            assert(output.request_id == UINT32_C(0xABCDEF01));
        }
        if (i % 500u == 0u) desk_packet_reset(&session);
    }

    printf("PASS: %u deterministic malformed radio packets, %u valid frames\n",
           30000u, accepted);
    return 0;
}
