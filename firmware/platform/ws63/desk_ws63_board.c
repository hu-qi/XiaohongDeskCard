/*
 * Board-level binding for the original Xiaohong AI WS63 desk card.
 *
 * Purpose: give the linker a real, always-reachable call path from the vendor
 * firmware's own startup (LvglTask) into the portable Desk Core. Without such a
 * reference the whole `static_library("xiaohong")` deskcard objects are dropped
 * by the linker, because `xiaohong` is a `ram_component` linked with normal
 * (non --whole-archive) semantics and nothing in the vendor tree calls desk_*.
 *
 * This file uses only vendor NV storage (uapi_nv_read/write) and the existing
 * g_disp_event_qid display queue. It deliberately does NOT register BLE/SLE/HTTP
 * transports: those still require real GATT/SSAP/HTTP service tables and are
 * reported as unimplemented rather than faked.
 */
#include "desk_ws63_board.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(DESK_BOARD_HOST_TEST)
/* Host build: no WS63 SDK. The NV HAL is supplied by the test through
 * desk_board_test_set_nv_io(); production code paths are unchanged. */
#include "desk_nv_store.h"
#include "desk_ws63_glue.h"
#else
/* Only the NV API and the error codes are needed here. The board poll is
 * driven directly by the vendor LvglTask, so no CMSIS queue is referenced. */
#include "errcode.h"
#include "nv.h"

#include "desk_nv_store.h"
#include "desk_ws63_glue.h"
#endif

/* Two NV keys in region 0 (0x0001..0x0FFF). Vendor already uses 0x1001 (Wi-Fi)
 * and 0xF000 (OTA); 0x0D01/0x0D02 are free and reserved for the desk journal. */
#define DESK_NV_KEY_SLOT0 0x0D01u
#define DESK_NV_KEY_SLOT1 0x0D02u

/* Vendor's display event queue is a uint8_t queue in LvglTask, so we do not
 * inject a new enum value. The board hook is polled directly from that task. */
#define DESK_DISP_UPDATE_EVENT 0u

typedef struct {
    desk_ws63_port_t port;
    desk_nv_store_t nv;
    bool ready;
} desk_board_t;

static desk_board_t g_board;

/* ---- NV HAL: two independent NV items, each holds one 248-byte slot. ---- */

#if !defined(DESK_BOARD_HOST_TEST)
static int nv_read(void *ctx, unsigned slot, size_t offset, uint8_t *data,
                   size_t size)
{
    (void)ctx;
    if (offset + size > DESK_NV_RECORD_SIZE) return 0;
    uint8_t buf[DESK_NV_RECORD_SIZE];
    uint16_t got = 0;
    const uint16_t key = slot == 0u ? DESK_NV_KEY_SLOT0 : DESK_NV_KEY_SLOT1;
    memset(buf, 0xFF, sizeof(buf));
    if (uapi_nv_read(key, sizeof(buf), &got, buf) != ERRCODE_SUCC) {
        /* First boot: item absent. Report an erased slot, not an I/O error. */
        memset(data, 0xFF, size);
        return 1;
    }
    if (got < offset + size) {
        memset(data, 0xFF, size);
        return 1;
    }
    memcpy(data, buf + offset, size);
    return 1;
}

static int nv_erase(void *ctx, unsigned slot)
{
    (void)ctx;
    /* NV has no sector erase; writing an all-0xFF record marks it empty, which
     * desk_nv_store interprets as an erased slot. */
    uint8_t blank[DESK_NV_RECORD_SIZE];
    memset(blank, 0xFF, sizeof(blank));
    const uint16_t key = slot == 0u ? DESK_NV_KEY_SLOT0 : DESK_NV_KEY_SLOT1;
    return uapi_nv_write(key, blank, (uint16_t)sizeof(blank)) == ERRCODE_SUCC;
}

static int nv_program(void *ctx, unsigned slot, size_t offset,
                      const uint8_t *data, size_t size)
{
    (void)ctx;
    if (offset + size > DESK_NV_RECORD_SIZE) return 0;
    uint8_t buf[DESK_NV_RECORD_SIZE];
    uint16_t got = 0;
    const uint16_t key = slot == 0u ? DESK_NV_KEY_SLOT0 : DESK_NV_KEY_SLOT1;
    memset(buf, 0xFF, sizeof(buf));
    if (uapi_nv_read(key, sizeof(buf), &got, buf) != ERRCODE_SUCC ||
        got < sizeof(buf)) {
        memset(buf, 0xFF, sizeof(buf));
    }
    memcpy(buf + offset, data, size);
    return uapi_nv_write(key, buf, (uint16_t)sizeof(buf)) == ERRCODE_SUCC;
}
#else
/* Host tests provide the NV HAL so the journal logic can be exercised without
 * the WS63 SDK. Production uses the uapi_nv_* implementation above. */
static desk_nv_io_t g_host_io;
static int g_host_io_set;

void desk_board_test_set_nv_io(desk_nv_io_t io)
{
    g_host_io = io;
    g_host_io_set = (io.read != NULL && io.erase != NULL && io.program != NULL);
}
#endif

/* ---- glue callbacks ---- */

static int board_load(void *ctx, desk_content_t *out, uint32_t *revision)
{
    (void)ctx;
    return desk_nv_load(&g_board.nv, out, revision);
}

static int board_save(void *ctx, const desk_content_t *value, uint32_t revision)
{
    (void)ctx;
    return desk_nv_save(&g_board.nv, value, revision);
}

static void board_post_display(void *ctx)
{
    /* No separate wake event is registered; the vendor LvglTask polls
     * desk_board_poll() every loop iteration, so this is a no-op. */
    (void)ctx;
}

static void board_enqueue_ack(void *ctx, desk_channel_t channel,
                              uint32_t peer_token,
                              const uint8_t ack[DESK_ACK_SIZE])
{
    /* No BLE/SLE/HTTP transport is registered in this build; ACK is dropped
     * rather than pretending a radio service exists. */
    (void)ctx; (void)channel; (void)peer_token; (void)ack;
}

static bool board_authorize(void *ctx, desk_channel_t channel,
                            uint32_t peer_token)
{
    /* No remote transport in this build, so no peer can ever be authorized. */
    (void)ctx; (void)channel; (void)peer_token;
    return false;
}

static void board_enqueue_status(void *ctx, desk_channel_t channel,
                                 uint32_t peer_token, desk_result_t result,
                                 const desk_http_state_t *state)
{
    (void)ctx; (void)channel; (void)peer_token; (void)result; (void)state;
}

/* ---- public entry points ---- */

int desk_board_init(void)
{
    if (g_board.ready) return 1;

#if defined(DESK_BOARD_HOST_TEST)
    if (!g_host_io_set) return 0;
    desk_nv_io_t io = g_host_io;
#else
    desk_nv_io_t io = {nv_read, nv_erase, nv_program, NULL};
#endif
    if (!desk_nv_init(&g_board.nv, io)) return 0;

    desk_ws63_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.load = board_load;
    cb.save = board_save;
    cb.post_display_event = board_post_display;
    cb.enqueue_ack = board_enqueue_ack;
    cb.authorize_on_ui = board_authorize;
    cb.enqueue_status = board_enqueue_status;
    cb.context = &g_board;

    if (!desk_ws63_port_init(&g_board.port, cb, NULL)) return 0;
    g_board.ready = true;
    return 1;
}

void desk_board_poll(void)
{
    if (!g_board.ready) return;
    (void)desk_ws63_port_ui_drain(&g_board.port);
}

int desk_board_is_ready(void)
{
    return g_board.ready ? 1 : 0;
}
