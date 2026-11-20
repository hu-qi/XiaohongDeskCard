/*
 * Host test for the SDK-facing adapters. Covers the parts that do not need the
 * WS63 stack: HTTP path matching, Bearer authorization, and the refusal paths
 * that must never reach the desk queue.
 */
#include "desk_ws63_adapters.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_packet.h"
#include "desk_wire.h"
#include "desk_ws63_board.h"

/* Feed one Desk Wire frame as the BEGIN/CHUNK/COMMIT packets the Harmony
 * client writes (DeskWireFrame.ets), through the desk command handle. */
static void feed_frame(const uint8_t *frame, size_t flen)
{
    uint8_t pkt[64];
    pkt[0] = DESK_PACKET_BEGIN;
    pkt[1] = (uint8_t)flen;
    pkt[2] = (uint8_t)(flen >> 8);
    const uint32_t c = desk_wire_crc32(frame, flen);
    pkt[3] = (uint8_t)c; pkt[4] = (uint8_t)(c >> 8);
    pkt[5] = (uint8_t)(c >> 16); pkt[6] = (uint8_t)(c >> 24);
    assert(desk_ble_on_write(0x0021u, pkt, 7u));

    const size_t chunk = 20u - DESK_PACKET_CHUNK_HEADER;
    for (size_t off = 0; off < flen; off += chunk) {
        const size_t n = (flen - off) < chunk ? (flen - off) : chunk;
        pkt[0] = DESK_PACKET_CHUNK;
        pkt[1] = (uint8_t)off;
        pkt[2] = (uint8_t)(off >> 8);
        memcpy(pkt + 3, frame + off, n);
        assert(desk_ble_on_write(0x0021u, pkt, 3u + n));
    }
    pkt[0] = DESK_PACKET_COMMIT;
    assert(desk_ble_on_write(0x0021u, pkt, 1u));
}

static uint8_t g_slots[2][DESK_NV_RECORD_SIZE];

static int nv_read(void *c, unsigned s, size_t o, uint8_t *d, size_t n)
{
    (void)c;
    if (s > 1u || o + n > DESK_NV_RECORD_SIZE) return 0;
    memcpy(d, g_slots[s] + o, n);
    return 1;
}
static int nv_erase(void *c, unsigned s)
{
    (void)c;
    if (s > 1u) return 0;
    memset(g_slots[s], 0xFF, DESK_NV_RECORD_SIZE);
    return 1;
}
static int nv_program(void *c, unsigned s, size_t o, const uint8_t *d, size_t n)
{
    (void)c;
    if (s > 1u || o + n > DESK_NV_RECORD_SIZE) return 0;
    memcpy(g_slots[s] + o, d, n);
    return 1;
}

int main(void)
{
    char out[DESK_HTTP_MAX_RESPONSE];
    size_t out_len = 0;

    /* Uninitialised board: every request is left to the vendor handler. */
    assert(desk_http_handle_request("PUT /api/v1/desk/content HTTP/1.1",
                                    "", NULL, 0, out, sizeof(out),
                                    &out_len) == DESK_HTTP_NOT_HANDLED);
    assert(!desk_ble_owns_handle(1u));
    assert(!desk_ble_flush_ack());

    memset(g_slots, 0xFF, sizeof(g_slots));
    desk_nv_io_t io = {nv_read, nv_erase, nv_program, NULL};
    desk_board_test_set_nv_io(io);
    assert(desk_board_init() == 1);

    /* Unrelated paths must not be claimed. */
    assert(desk_http_handle_request("GET / HTTP/1.1", "", NULL, 0, out,
                                    sizeof(out), &out_len) ==
           DESK_HTTP_NOT_HANDLED);
    assert(desk_http_handle_request("POST /api/v1/desk/content HTTP/1.1", "",
                                    NULL, 0, out, sizeof(out), &out_len) ==
           DESK_HTTP_NOT_HANDLED);
    /* Prefix collision must not match. */
    assert(desk_http_handle_request("PUT /api/v1/desk/contentX HTTP/1.1", "",
                                    NULL, 0, out, sizeof(out), &out_len) ==
           DESK_HTTP_NOT_HANDLED);

    /* Correct path but no token -> 401. */
    assert(desk_http_handle_request("GET /api/v1/desk/status HTTP/1.1",
                                    "Host: x\r\n", NULL, 0, out, sizeof(out),
                                    &out_len) == DESK_HTTP_HANDLED_UNAUTH);
    assert(desk_http_handle_request("GET /api/v1/desk/status HTTP/1.1",
                                    "Authorization: Basic abc\r\n", NULL, 0,
                                    out, sizeof(out), &out_len) ==
           DESK_HTTP_HANDLED_UNAUTH);

    /* No token provisioned yet -> still 401 even with a Bearer header. */
    assert(desk_http_handle_request("GET /api/v1/desk/status HTTP/1.1",
                                    "Authorization: Bearer abc\r\n", NULL, 0,
                                    out, sizeof(out), &out_len) ==
           DESK_HTTP_HANDLED_UNAUTH);

    /* Provision a token, then a wrong one is rejected and a right one passes
     * the auth gate (it then returns OK with no body, since the UI task has
     * not run yet). */
    desk_transport_t *t = desk_board_transport();
    assert(t != NULL);
    assert(desk_transport_set_http_token(t, "tok123", 6u));

    assert(desk_http_handle_request("GET /api/v1/desk/status HTTP/1.1",
                                    "Authorization: Bearer nope\r\n", NULL, 0,
                                    out, sizeof(out), &out_len) ==
           DESK_HTTP_HANDLED_UNAUTH);

    out_len = 0;
    assert(desk_http_handle_request("GET /api/v1/desk/status HTTP/1.1",
                                    "Authorization: Bearer tok123\r\n", NULL, 0,
                                    out, sizeof(out), &out_len) ==
           DESK_HTTP_HANDLED_OK);
    assert(out_len == 0u); /* queued, UI task has not produced a snapshot */

    /* Header lookup is case-insensitive and tolerates leading spaces. */
    out_len = 0;
    assert(desk_http_handle_request("GET /api/v1/desk/status HTTP/1.1",
                                    "authorization:   Bearer tok123\r\n", NULL,
                                    0, out, sizeof(out), &out_len) ==
           DESK_HTTP_HANDLED_OK);

    /* BLE handle routing: nothing registered, so nothing is claimed. */
    assert(!desk_ble_on_write(1u, (const uint8_t *)"x", 1u));
    assert(!desk_sle_on_write(1u, 1u, (const uint8_t *)"x", 1u));
    assert(!desk_sle_owns_handle(1u));
    assert(!desk_sle_flush_ack());

    /* BLE: register the handles the service table would produce. */
    desk_ble_test_set_handles(0x0021u, 0x0023u);
    assert(desk_ble_owns_handle(0x0021u));
    assert(!desk_ble_owns_handle(0x0022u));
    /* A write on our handle is accepted; on a foreign handle it is refused so
     * the vendor's provisioning callback keeps it. */
    assert(desk_ble_on_write(0x0021u, (const uint8_t *)"x", 1u));
    assert(!desk_ble_on_write(0x0022u, (const uint8_t *)"x", 1u));

    /* SLE: same routing rules, independent handle space. */
    desk_sle_test_set_handles(0x0041u, 0x0043u);
    assert(desk_sle_owns_handle(0x0041u));
    assert(!desk_sle_owns_handle(0x0043u));
    assert(desk_sle_on_write(1u, 0x0041u, (const uint8_t *)"x", 1u));
    assert(!desk_sle_on_write(1u, 0x0042u, (const uint8_t *)"x", 1u));

    /* BLE 128-bit UUID byte order. The stack stores little-endian, so the
     * array must be the reverse of the string order the header macros use.
     * Getting this wrong publishes a different UUID and the Harmony client
     * cannot find the characteristic at all. */
    {
        static const uint8_t svc[16] = DESK_BLE_SERVICE_UUID;
        uint8_t le[16];
        desk_uuid128_le(svc, le);
        /* 79b468bd-306c-58a8-9415-c9e9c993bdfa reversed is
         * fa bd 93 c9 e9 c9 15 94 a8 58 6c 30 bd 68 b4 79 */
        static const uint8_t expect[16] = {
            0xfa, 0xbd, 0x93, 0xc9, 0xe9, 0xc9, 0x15, 0x94,
            0xa8, 0x58, 0x6c, 0x30, 0xbd, 0x68, 0xb4, 0x79
        };
        assert(memcmp(le, expect, sizeof(expect)) == 0);
        /* Round-trips back to string order. */
        uint8_t back[16];
        desk_uuid128_le(le, back);
        assert(memcmp(back, svc, sizeof(svc)) == 0);
    }

    /* SLE advertising uses [type][length][value]; BLE uses
     * [length][type][value]. Mixing them makes the stack drop the block, so
     * the device never shows up in a scan. Assert the layout the announce code
     * builds. */
    {
        static const char name[] = "XiaohongDesk";
        uint8_t rsp[2 + sizeof(name) - 1u];
        rsp[0] = 0x0Bu;                          /* type: local name */
        rsp[1] = (uint8_t)(sizeof(name) - 1u);   /* length */
        memcpy(&rsp[2], name, sizeof(name) - 1u);
        assert(rsp[0] == 0x0Bu);
        assert(rsp[1] == 12u);
        assert(memcmp(&rsp[2], "XiaohongDesk", 12u) == 0);
        /* A BLE-ordered block would put the length (12) in the type slot,
         * where 12 is not a valid SLE AD type. */
        assert(rsp[0] != rsp[1]);
    }

    /* Two consecutive commands on one BLE connection. The link marks itself
     * "awaiting ACK" when a COMMIT is accepted; if that flag is never cleared
     * the second write is rejected with DESK_RADIO_AWAITING_ACK and the card
     * accepts exactly one update per connection. desk_ble_flush_ack() must
     * clear it once the reply leaves the mailbox. */
    {
        desk_ble_test_set_handles(0x0021u, 0x0023u);
        desk_ble_on_connect(7u);

        desk_set_command_t cmd = {0};
        strcpy(cmd.content.name, "张三");
        strcpy(cmd.content.title, "工程师");
        strcpy(cmd.content.organization, "小鸿科技");
        strcpy(cmd.content.theme, "classic");

        uint8_t frame[DESK_WIRE_MAX_FRAME];
        size_t flen = 0;
        cmd.request_id = 11u;
        cmd.expected_revision = 0u;
        assert(desk_wire_encode_set(&cmd, frame, sizeof(frame), &flen) ==
               DESK_WIRE_OK);
        feed_frame(frame, flen);
        desk_board_poll();                 /* UI task applies + queues the ACK */
        assert(desk_ble_flush_ack());      /* ACK leaves the mailbox */

        /* Same connection, second command: revision 1 after the first apply. */
        cmd.request_id = 12u;
        cmd.expected_revision = 1u;
        assert(desk_wire_encode_set(&cmd, frame, sizeof(frame), &flen) ==
               DESK_WIRE_OK);
        feed_frame(frame, flen);
        desk_board_poll();
        assert(desk_ble_flush_ack());

        desk_ble_on_disconnect(7u);
    }

    printf("desk_ws63_adapters_tests: OK\n");
    return 0;
}
