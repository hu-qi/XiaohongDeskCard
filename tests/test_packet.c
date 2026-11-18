#include "desk_packet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, size_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16);
    p[3] = (uint8_t)(n >> 24);
}
static void begin_packet(uint8_t begin[7], const uint8_t *frame, size_t length)
{
    begin[0] = DESK_PACKET_BEGIN;
    put16(begin + 1, length);
    put32(begin + 3, desk_wire_crc32(frame, length));
}
static void send_frame(desk_packet_session_t *session, const uint8_t *frame,
                       size_t length, uint32_t at, desk_set_command_t *out)
{
    uint8_t begin[7];
    begin_packet(begin, frame, length);
    assert(desk_packet_feed(session, begin, sizeof(begin), at++, 500u, out)
           == DESK_PACKET_PROGRESS);
    size_t offset = 0;
    while (offset < length) {
        uint8_t chunk[20];
        size_t n = length - offset;
        if (n > sizeof(chunk) - 3u) n = sizeof(chunk) - 3u;
        chunk[0] = DESK_PACKET_CHUNK;
        put16(chunk + 1, offset);
        memcpy(chunk + 3, frame + offset, n);
        assert(desk_packet_feed(session, chunk, n + 3u, at++, 500u, out)
               == DESK_PACKET_PROGRESS);
        offset += n;
    }
    const uint8_t commit = DESK_PACKET_COMMIT;
    assert(desk_packet_feed(session, &commit, 1u, at, 500u, out)
           == DESK_PACKET_READY);
}

int main(void)
{
    desk_set_command_t input = {0};
    input.request_id = 23u;
    input.expected_revision = 8u;
    strcpy(input.content.name, "张三");
    strcpy(input.content.title, "负责人");
    strcpy(input.content.organization, "OpenHarmony");
    strcpy(input.content.theme, "classic");
    uint8_t frame[DESK_WIRE_MAX_FRAME] = {0};
    size_t n = 0;
    assert(desk_wire_encode_set(&input, frame, sizeof(frame), &n) == DESK_WIRE_OK);
    desk_packet_session_t session = {0};
    desk_set_command_t output = {.request_id = 999u};
    const uint8_t commit = DESK_PACKET_COMMIT;

    assert(desk_packet_feed(&session, &commit, 1u, 1u, 500u, &output)
           == DESK_PACKET_BAD_STATE);
    assert(output.request_id == 999u);
    send_frame(&session, frame, n, 100u, &output);
    assert(output.request_id == 23u);
    assert(output.expected_revision == 8u);
    assert(strcmp(output.content.name, "张三") == 0);
    assert(!session.rx.active);

    /* A second independent BLE/SLE session can receive a separate frame. */
    desk_packet_session_t second = {0};
    output.request_id = 999u;
    send_frame(&second, frame, n, 1000u, &output);
    assert(output.request_id == 23u);
    assert(!second.rx.active);

    /* CRC mismatch after a single modified payload byte. */
    uint8_t begin[7];
    begin_packet(begin, frame, n);
    assert(desk_packet_feed(&session, begin, sizeof(begin), 200u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    uint8_t chunk[DESK_WIRE_MAX_FRAME + 3] = {0};
    chunk[0] = DESK_PACKET_CHUNK;
    put16(chunk + 1, 0u);
    memcpy(chunk + 3, frame, n);
    chunk[n + 2] ^= 1u;
    assert(desk_packet_feed(&session, chunk, n + 3u, 201u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    assert(desk_packet_feed(&session, &commit, 1u, 202u, 500u, &output)
           == DESK_PACKET_BAD_CRC);
    assert(output.request_id == 23u); /* unchanged */

    /* Out-of-order, malformed, missing chunk, and new BEGIN recovery. */
    assert(desk_packet_feed(&session, begin, sizeof(begin), 300u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    put16(chunk + 1, 1u);
    assert(desk_packet_feed(&session, chunk, 4u, 301u, 500u, &output)
           == DESK_PACKET_BAD_STATE);
    assert(!session.rx.active);
    assert(desk_packet_feed(&session, begin, sizeof(begin), 302u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    assert(desk_packet_feed(&session, &commit, 1u, 303u, 500u, &output)
           == DESK_PACKET_BAD_STATE);
    assert(!session.rx.active);
    assert(desk_packet_feed(&session, begin, sizeof(begin), 304u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    uint8_t bad = 0x77u;
    assert(desk_packet_feed(&session, &bad, 1u, 305u, 500u, &output)
           == DESK_PACKET_BAD_INPUT);

    /* No command can survive an inactive session timeout. */
    assert(desk_packet_feed(&session, begin, sizeof(begin), 400u, 100u, &output)
           == DESK_PACKET_PROGRESS);
    chunk[0] = DESK_PACKET_CHUNK;
    put16(chunk + 1, 0u);
    memcpy(chunk + 3, frame, n);
    assert(desk_packet_feed(&session, chunk, n + 3u, 500u, 100u, &output)
           == DESK_PACKET_BAD_STATE);

    /* Uint32 clock wraparound: +38ms remains within the 40ms window. */
    assert(desk_packet_feed(&session, begin, sizeof(begin),
                            UINT32_MAX - 25u, 40u, &output) == DESK_PACKET_PROGRESS);
    assert(desk_packet_feed(&session, chunk, n + 3u, 12u, 40u, &output)
           == DESK_PACKET_PROGRESS);
    assert(desk_packet_feed(&session, &commit, 1u, 13u, 40u, &output)
           == DESK_PACKET_READY);

    /* Full frame CRC can pass, yet inner Desk Wire payload CRC must fail. */
    uint8_t modified[DESK_WIRE_MAX_FRAME] = {0};
    memcpy(modified, frame, n);
    modified[n - 1u] ^= 1u;
    begin_packet(begin, modified, n);
    assert(desk_packet_feed(&session, begin, 7u, 600u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    memcpy(chunk + 3, modified, n);
    assert(desk_packet_feed(&session, chunk, n + 3u, 601u, 500u, &output)
           == DESK_PACKET_PROGRESS);
    assert(desk_packet_feed(&session, &commit, 1u, 602u, 500u, &output)
           == DESK_PACKET_BAD_CRC);

    printf("PASS: packet state/partial/CRC/timeout/wraparound/independent session\n");
    return 0;
}
