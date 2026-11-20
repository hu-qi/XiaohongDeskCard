/*
 * End-to-end host test for the transport wiring.
 *
 * It builds the exact BEGIN/CHUNK/COMMIT packets the Harmony client produces
 * (apps/harmony/.../DeskWireFrame.ets), feeds them through the BLE path, drains
 * the dispatcher as the UI task would, and checks the ACK bytes decode with the
 * same 16-byte layout the client's DeskAckFrame expects.
 */
#include "desk_ws63_transport.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_core.h"
#include "desk_dispatcher.h"
#include "desk_nv_store.h"
#include "desk_packet.h"
#include "desk_wire.h"

/* ---- fake NV backing store ---- */

static uint8_t g_slots[2][DESK_NV_RECORD_SIZE];

static int nv_read(void *c, unsigned slot, size_t off, uint8_t *d, size_t n)
{
    (void)c;
    if (slot > 1u || off + n > DESK_NV_RECORD_SIZE) return 0;
    memcpy(d, g_slots[slot] + off, n);
    return 1;
}
static int nv_erase(void *c, unsigned slot)
{
    (void)c;
    if (slot > 1u) return 0;
    memset(g_slots[slot], 0xFF, DESK_NV_RECORD_SIZE);
    return 1;
}
static int nv_program(void *c, unsigned slot, size_t off, const uint8_t *d,
                      size_t n)
{
    (void)c;
    if (slot > 1u || off + n > DESK_NV_RECORD_SIZE) return 0;
    memcpy(g_slots[slot] + off, d, n);
    return 1;
}

/* ---- core hooks ---- */

static desk_nv_store_t g_nv;
static int g_rendered;

static int hook_load(void *u, desk_content_t *out, uint32_t *rev)
{
    return desk_nv_load(u, out, rev);
}
static int hook_save(void *u, const desk_content_t *v, uint32_t rev)
{
    return desk_nv_save(u, v, rev);
}
static int hook_render(void *u, const desk_content_t *v)
{
    (void)u; (void)v;
    g_rendered++;
    return 1;
}

/* ---- dispatcher hooks ---- */

static desk_transport_t g_transport;

static void ui_notify(void *c) { (void)c; }
static void ui_result(void *c, desk_channel_t ch, uint32_t peer,
                      const uint8_t ack[DESK_ACK_SIZE])
{
    (void)c;
    (void)desk_reply_push_ack(&g_transport.replies, ch, peer, ack);
}
static bool ui_authorize(void *c, desk_channel_t ch, uint32_t peer)
{
    (void)c;
    /* Mirror production: only a live generation is authorized. */
    if (ch == DESK_CHANNEL_BLE) return peer == g_transport.ble_generation;
    if (ch == DESK_CHANNEL_SLE) return peer == g_transport.sle_generation;
    return true;
}
static bool ui_read_display(void *c, desk_content_t *out)
{
    (void)c;
    /* The board binding records the last rendered content; the test mirrors
     * that so a GET produces a full status snapshot. */
    out->name[0] = 'A'; out->name[1] = '\0';
    out->title[0] = 'B'; out->title[1] = '\0';
    out->organization[0] = 'C'; out->organization[1] = '\0';
    strcpy(out->theme, "classic");
    return true;
}
static void ui_status(void *c, desk_channel_t ch, uint32_t peer,
                      desk_result_t r, const desk_http_state_t *s)
{
    (void)c;
    (void)desk_reply_push_status(&g_transport.replies, ch, peer, r, s);
}

/* ---- client-side frame builder, mirroring DeskWireFrame.ets ---- */

static uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ 0xFFFFFFFFu;
}

static size_t build_frame(uint32_t request_id, uint32_t expected_rev,
                          const char *name, const char *title,
                          const char *org, const char *theme,
                          uint8_t *out, size_t cap)
{
    const char *fields[4] = {name, title, org, theme};
    uint8_t payload[384];
    /* Each field is one length byte plus its bytes, so the payload is the sum
     * of (1 + len) - matching both desk_wire_encode_set and DeskWireFrame.ets. */
    size_t plen = 0;
    for (int i = 0; i < 4; i++) {
        const size_t n = strlen(fields[i]);
        payload[plen++] = (uint8_t)n;
        memcpy(payload + plen, fields[i], n);
        plen += n;
    }
    const size_t total = DESK_WIRE_HEADER_SIZE + plen;
    if (total > cap) return 0;

    memset(out, 0, total);
    out[0] = 0xD5; out[1] = 1; out[2] = 1; out[3] = 0;
    out[4] = (uint8_t)request_id; out[5] = (uint8_t)(request_id >> 8);
    out[6] = (uint8_t)(request_id >> 16); out[7] = (uint8_t)(request_id >> 24);
    out[8] = (uint8_t)expected_rev; out[9] = (uint8_t)(expected_rev >> 8);
    out[10] = (uint8_t)(expected_rev >> 16); out[11] = (uint8_t)(expected_rev >> 24);
    out[12] = (uint8_t)plen; out[13] = (uint8_t)(plen >> 8);
    memcpy(out + DESK_WIRE_HEADER_SIZE, payload, plen);
    const uint32_t c = crc32(out + DESK_WIRE_HEADER_SIZE, plen);
    out[14] = (uint8_t)c; out[15] = (uint8_t)(c >> 8);
    out[16] = (uint8_t)(c >> 16); out[17] = (uint8_t)(c >> 24);
    return total;
}

static void feed_packets(desk_transport_t *t, uint32_t gen, const uint8_t *frame,
                         size_t flen, uint32_t now)
{
    uint8_t pkt[64];
    pkt[0] = DESK_PACKET_BEGIN;
    pkt[1] = (uint8_t)flen; pkt[2] = (uint8_t)(flen >> 8);
    const uint32_t c = crc32(frame, flen);
    pkt[3] = (uint8_t)c; pkt[4] = (uint8_t)(c >> 8);
    pkt[5] = (uint8_t)(c >> 16); pkt[6] = (uint8_t)(c >> 24);
    assert(desk_transport_ble_write(t, gen, pkt, 7, now) == DESK_RADIO_PROGRESS);

    const size_t chunk = 20 - 3;
    for (size_t off = 0; off < flen; off += chunk) {
        const size_t n = (flen - off) < chunk ? (flen - off) : chunk;
        pkt[0] = DESK_PACKET_CHUNK;
        pkt[1] = (uint8_t)off; pkt[2] = (uint8_t)(off >> 8);
        memcpy(pkt + 3, frame + off, n);
        assert(desk_transport_ble_write(t, gen, pkt, 3 + n, now) ==
               DESK_RADIO_PROGRESS);
    }
    pkt[0] = DESK_PACKET_COMMIT;
    assert(desk_transport_ble_write(t, gen, pkt, 1, now) == DESK_RADIO_QUEUED);
}

int main(void)
{
    memset(g_slots, 0xFF, sizeof(g_slots));
    desk_nv_io_t io = {nv_read, nv_erase, nv_program, NULL};
    assert(desk_nv_init(&g_nv, io));

    desk_hooks_t hooks = {hook_load, hook_save, hook_render, &g_nv};
    desk_core_t core;
    desk_core_init(&core, hooks);

    desk_dispatcher_t dispatcher;
    desk_dispatch_hooks_t dh = {0};
    dh.notify_ui = ui_notify;
    dh.on_result = ui_result;
    dh.authorize_on_ui = ui_authorize;
    dh.read_display_on_ui = ui_read_display;
    dh.on_status = ui_status;
    assert(desk_dispatcher_init(&dispatcher, &core, dh));

    desk_transport_config_t cfg = {&dispatcher, &core, NULL, NULL, NULL};
    assert(desk_transport_init(&g_transport, &cfg));

    /* --- BLE write path --- */
    const uint32_t gen = desk_transport_ble_open(&g_transport);
    assert(gen != 0u);
    g_transport.ble.authorized = true;  /* ownership handshake succeeded */

    uint8_t frame[384];
    size_t flen = build_frame(7u, 0u, "张三", "工程师", "小鸿科技", "classic",
                              frame, sizeof(frame));
    assert(flen > DESK_WIRE_HEADER_SIZE);
    feed_packets(&g_transport, gen, frame, flen, 1000u);

    /* UI task drains the queue. */
    assert(desk_dispatcher_process_one(&dispatcher));

    uint8_t ack[DESK_ACK_SIZE];
    assert(desk_transport_ble_take_ack(&g_transport, gen, ack));
    desk_ack_t decoded;
    assert(desk_wire_decode_ack(ack, DESK_ACK_SIZE, &decoded) == DESK_WIRE_OK);
    assert(decoded.result == DESK_OK);
    assert(decoded.request_id == 7u);
    assert(decoded.revision == 1u);

    /* --- stale generation is rejected --- */
    assert(desk_transport_ble_write(&g_transport, gen + 99u, frame, flen,
                                    2000u) == DESK_RADIO_NOT_CONNECTED);

    /* --- HTTP path --- */
    assert(desk_transport_set_http_token(&g_transport, "s3cret-token", 12u));
    assert(desk_transport_http_authorized(&g_transport, "s3cret-token", 12u));
    assert(!desk_transport_http_authorized(&g_transport, "wrong", 5u));
    assert(!desk_transport_http_authorized(&g_transport, "s3cret-token", 11u));

    const char *body =
        "{\"requestId\":9,\"expectedRevision\":1,"
        "\"content\":{\"name\":\"李四\",\"title\":\"经理\","
        "\"organization\":\"小鸿\",\"theme\":\"modern\"}}";
    uint32_t req_id = 0;
    assert(desk_transport_http_put(&g_transport, "s3cret-token", 12u,
                                   (const uint8_t *)body, strlen(body),
                                   &req_id) == 0);
    assert(req_id == 9u);
    assert(desk_dispatcher_process_one(&dispatcher));

    uint32_t peer = 0;
    assert(desk_transport_http_get(&g_transport, "s3cret-token", 12u, &peer) == 0);
    assert(desk_dispatcher_process_one(&dispatcher));

    desk_reply_item_t reply;
    assert(desk_transport_http_take_reply(&g_transport, peer, &reply));
    assert(reply.result == DESK_OK);

    /* --- unauthorized HTTP is refused before touching the queue --- */
    assert(desk_transport_http_put(&g_transport, "bad", 3u,
                                   (const uint8_t *)body, strlen(body),
                                   &req_id) == -401);

    /* --- disconnect frees only that peer's replies --- */
    desk_transport_ble_close(&g_transport, gen);
    assert(g_transport.ble_generation == 0u);

    printf("desk_ws63_transport_tests: OK (renders=%d)\n", g_rendered);
    return 0;
}
