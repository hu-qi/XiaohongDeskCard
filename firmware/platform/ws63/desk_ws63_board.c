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
#include "desk_ws63_transport.h"
#else
/* Only the NV API and the error codes are needed here. The board poll is
 * driven directly by the vendor LvglTask, so no CMSIS queue is referenced.
 * osKernelGetTickCount()/TickFreq() come from CMSIS-RTOS2. */
#include "cmsis_os2.h"
#include "errcode.h"
#include "nv.h"

#include "desk_nv_store.h"
#include "desk_ws63_adapters.h"
#include "desk_ws63_glue.h"
#include "desk_ws63_http.h"
#include "desk_ws63_transport.h"

/* CMSIS ticks are not guaranteed to be 1 ms; convert to milliseconds for the
 * announce keepalive. */
static uint32_t board_now_ms(void)
{
    uint32_t freq = osKernelGetTickFreq();
    if (freq == 0u) freq = 1u;
    return (uint32_t)(((uint64_t)osKernelGetTickCount() * 1000u) / freq);
}
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
    desk_transport_t transport;
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
    (void)ctx;
    (void)desk_reply_push_ack(&g_board.transport.replies, channel, peer_token,
                              ack);
}

static bool board_authorize(void *ctx, desk_channel_t channel,
                            uint32_t peer_token)
{
    (void)ctx;
    /* Re-check the live generation when the UI task actually runs, so a peer
     * that disconnected while its request sat in the queue is rejected. */
    switch (channel) {
        case DESK_CHANNEL_BLE:
            return peer_token != 0u &&
                   peer_token == g_board.transport.ble_generation;
        case DESK_CHANNEL_SLE:
            return peer_token != 0u &&
                   peer_token == g_board.transport.sle_generation;
        case DESK_CHANNEL_WIFI:
            return peer_token != 0u;
        default:
            return false;
    }
}

static void board_enqueue_status(void *ctx, desk_channel_t channel,
                                 uint32_t peer_token, desk_result_t result,
                                 const desk_http_state_t *state)
{
    (void)ctx;
    (void)desk_reply_push_status(&g_board.transport.replies, channel,
                                 peer_token, result, state);
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

    /* Transport adapters submit through the port's own dispatcher so the UI
     * task keeps a single owner queue for LVGL and Desk Core. */
    desk_transport_config_t tcfg = {
        desk_ws63_port_dispatcher(&g_board.port),
        &g_board.port.core,
        NULL, NULL, NULL
    };
    if (!desk_transport_init(&g_board.transport, &tcfg)) return 0;

    g_board.ready = true;

#if !defined(DESK_BOARD_HOST_TEST)
    /* All three transports are started from the UI task, not from the vendor's
     * BLE provisioning task. That task only runs on a device with no Wi-Fi
     * config, so anything started from it is missing on a normally provisioned
     * device.
     *
     * BLE is started from the poll rather than here, because the vendor may
     * still be bringing Wi-Fi up; desk_ble_poll() waits for the station to be
     * connected and is idempotent. The HTTP listener waits for the station to
     * get an IP by itself. */
    desk_sle_start();
    (void)desk_http_task_start();
#endif
    return 1;
}

desk_transport_t *desk_board_transport(void)
{
    return g_board.ready ? &g_board.transport : NULL;
}

void desk_board_poll(void)
{
    if (!g_board.ready) return;
    (void)desk_ws63_port_ui_drain(&g_board.port);
#if !defined(DESK_BOARD_HOST_TEST)
    /* Bring BLE up once the device is on Wi-Fi, and hand it back if the device
     * falls into provisioning. Cheap and idempotent. */
    desk_ble_poll();

    /* Keep SLE announce alive: the WS63 stack may stop advertising after a
     * short period if no scanner is actively seeking. */
    desk_sle_announce_keepalive(board_now_ms());

    /* The ACKs Desk Core just produced must go out on whichever link asked.
     * The vendor only runs its BLE provisioning task while there is no Wi-Fi
     * config, so the UI task is the one place that always runs: pump from
     * here rather than from a radio worker. */
    (void)desk_radio_flush_acks();
#endif
}

int desk_board_is_ready(void)
{
    return g_board.ready ? 1 : 0;
}
