/*
 * Host test for the SDK-facing adapters. Covers the parts that do not need the
 * WS63 stack: HTTP path matching, Bearer authorization, and the refusal paths
 * that must never reach the desk queue.
 */
#include "desk_ws63_adapters.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_ws63_board.h"

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

    printf("desk_ws63_adapters_tests: OK\n");
    return 0;
}
