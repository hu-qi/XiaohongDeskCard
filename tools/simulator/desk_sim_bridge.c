/*
 * Desktop-only bridge for XiaohongDeskCard's REAL C99 Desk Core.
 * No WS63, LVGL or radio SDK is involved.
 */
#include "desk_core.h"
#include "desk_wire.h"
#include "desk_packet.h"
#include "desk_dispatcher.h"
#include "desk_radio_ingress.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SNAPSHOT_MAGIC UINT32_C(0x58484443)
#define SNAPSHOT_PATH_CAP 512u

typedef struct {
    uint32_t magic;
    uint32_t revision;
    desk_content_t content;
    uint32_t crc;
} snapshot_t;

static desk_core_t g_core;
static desk_content_t g_displayed;
static char g_storage_path[SNAPSHOT_PATH_CAP];
static int g_initialized;
static desk_dispatcher_t g_dispatcher;
static uint32_t g_session_counter;

static struct {
    int valid;
    desk_channel_t channel;
    uint32_t peer_token;
    uint8_t ack[DESK_ACK_SIZE];
} g_transport_reply;

static void on_radio_reply(void *context, desk_channel_t channel,
                           uint32_t peer_token, const uint8_t ack[DESK_ACK_SIZE])
{
    (void)context;
    g_transport_reply.valid = 1;
    g_transport_reply.channel = channel;
    g_transport_reply.peer_token = peer_token;
    memcpy(g_transport_reply.ack, ack, DESK_ACK_SIZE);
}


static int load_snapshot(void *user, desk_content_t *out, uint32_t *revision)
{
    (void)user;
    if (g_storage_path[0] == '\0') return 0;
    FILE *file = fopen(g_storage_path, "rb");
    if (file == NULL) return 0;
    snapshot_t snapshot = {0};
    const size_t read_count = fread(&snapshot, 1, sizeof(snapshot), file);
    const int extra = fgetc(file);
    const int close_result = fclose(file);
    if (read_count != sizeof(snapshot) || extra != EOF || close_result != 0 ||
        snapshot.magic != SNAPSHOT_MAGIC) return 0;
    const uint32_t crc = desk_wire_crc32((const uint8_t *)&snapshot,
                                         offsetof(snapshot_t, crc));
    if (crc != snapshot.crc || !desk_content_valid(&snapshot.content)) return 0;
    *out = snapshot.content;
    *revision = snapshot.revision;
    return 1;
}

static int save_snapshot(void *user, const desk_content_t *content, uint32_t revision)
{
    (void)user;
    if (g_storage_path[0] == '\0') return 0;
    char temporary[SNAPSHOT_PATH_CAP + 8u];
    const int n = snprintf(temporary, sizeof(temporary), "%s.tmp", g_storage_path);
    if (n <= 0 || (size_t)n >= sizeof(temporary)) return 0;
    snapshot_t snapshot = {0};
    snapshot.magic = SNAPSHOT_MAGIC;
    snapshot.revision = revision;
    snapshot.content = *content;
    snapshot.crc = desk_wire_crc32((const uint8_t *)&snapshot,
                                   offsetof(snapshot_t, crc));
    FILE *file = fopen(temporary, "wb");
    if (file == NULL) return 0;
    int ok = fwrite(&snapshot, 1, sizeof(snapshot), file) == sizeof(snapshot);
    if (fflush(file) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok || rename(temporary, g_storage_path) != 0) {
        (void)remove(temporary);
        return 0;
    }
    return 1;
}

static int render_screen(void *user, const desk_content_t *content)
{
    (void)user;
    g_displayed = *content;
    return 1;
}

int desk_sim_init(const char *path)
{
    if (path == NULL) return 0;
    const size_t n = strlen(path);
    if (n == 0 || n >= sizeof(g_storage_path)) return 0;
    memcpy(g_storage_path, path, n + 1u);
    memset(&g_displayed, 0, sizeof(g_displayed));
    desk_hooks_t hooks = {load_snapshot, save_snapshot, render_screen, NULL};
    desk_core_init(&g_core, hooks);
    desk_dispatch_hooks_t dispatch_hooks = {0};
    dispatch_hooks.on_result = on_radio_reply;
    if (!desk_dispatcher_init(&g_dispatcher, &g_core, dispatch_hooks))
        return 0;
    memset(&g_transport_reply, 0, sizeof(g_transport_reply));
    g_initialized = 1;
    return desk_core_repaint(&g_core) == DESK_OK;
}

int desk_sim_apply_set(uint32_t request_id, uint32_t expected_revision,
                       const char *name, const char *title,
                       const char *organization, const char *theme, int authorized)
{
    if (!g_initialized || name == NULL || title == NULL ||
        organization == NULL || theme == NULL) return DESK_BAD_INPUT;
    desk_set_command_t cmd = {0};
    cmd.request_id = request_id;
    cmd.expected_revision = expected_revision;
    const char *sources[] = {name, title, organization, theme};
    char *targets[] = {cmd.content.name, cmd.content.title,
                       cmd.content.organization, cmd.content.theme};
    const size_t capacities[] = {DESK_NAME_CAP, DESK_TITLE_CAP,
                                 DESK_ORG_CAP, DESK_THEME_CAP};
    for (size_t i = 0; i < 4u; ++i) {
        const size_t n = strlen(sources[i]);
        if (n >= capacities[i]) return DESK_BAD_INPUT;
        memcpy(targets[i], sources[i], n + 1u);
    }
    return desk_core_apply(&g_core, &cmd, authorized != 0);
}

int desk_sim_apply_wire(const uint8_t *bytes, size_t size, int authorized)
{
    if (!g_initialized) return DESK_BAD_INPUT;
    desk_set_command_t cmd = {0};
    if (desk_wire_decode_set(bytes, size, &cmd) != DESK_WIRE_OK) return DESK_BAD_INPUT;
    return desk_core_apply(&g_core, &cmd, authorized != 0);
}

/*
 * Simulates transport chunking; deliberately does NOT create Bluetooth or SLE
 * connections. The very same bounded C receiver is used in firmware design.
 */
/*
 * Simulated BLE/SLE radio writes now take the SAME path as a future WS63
 * connection: raw BEGIN/CHUNK/COMMIT -> authenticated link -> dispatcher ->
 * Desk Core -> business ACK -> link completion. Still no physical radio.
 */
int desk_sim_apply_fragmented(const uint8_t *bytes, size_t size,
                              size_t max_write_bytes, int channel, int authorized)
{
    if (!g_initialized || bytes == NULL ||
        size < DESK_WIRE_HEADER_SIZE || size > DESK_WIRE_MAX_FRAME ||
        max_write_bytes < DESK_PACKET_BEGIN_SIZE ||
        (channel != DESK_CHANNEL_BLE && channel != DESK_CHANNEL_SLE))
        return DESK_BAD_INPUT;

    g_session_counter++;
    if (g_session_counter == 0u) g_session_counter = 1u;
    const uint32_t peer_token = g_session_counter;
    desk_radio_link_t link = {0};
    if (!desk_radio_link_open(&link, &g_dispatcher, (desk_channel_t)channel,
                              peer_token)) return DESK_BAD_INPUT;
    desk_radio_link_set_authorized(&link, authorized != 0);
    memset(&g_transport_reply, 0, sizeof(g_transport_reply));

    uint8_t begin[DESK_PACKET_BEGIN_SIZE] = {DESK_PACKET_BEGIN};
    begin[1] = (uint8_t)size;
    begin[2] = (uint8_t)(size >> 8u);
    const uint32_t crc = desk_wire_crc32(bytes, size);
    begin[3] = (uint8_t)crc;
    begin[4] = (uint8_t)(crc >> 8u);
    begin[5] = (uint8_t)(crc >> 16u);
    begin[6] = (uint8_t)(crc >> 24u);

    uint32_t now_ms = 1u;
    const uint32_t timeout_ms = 2000u;
    desk_radio_result_t status =
        desk_radio_link_receive(&link, begin, sizeof(begin), now_ms++, timeout_ms);
    if (status == DESK_RADIO_NOT_AUTHORIZED) {
        desk_radio_link_close(&link);
        return DESK_UNAUTHORIZED;
    }
    if (status != DESK_RADIO_PROGRESS) {
        desk_radio_link_close(&link);
        return DESK_BAD_INPUT;
    }

    for (size_t offset = 0u; offset < size;) {
        uint8_t packet[DESK_WIRE_MAX_FRAME + DESK_PACKET_CHUNK_HEADER];
        const size_t capacity = max_write_bytes - DESK_PACKET_CHUNK_HEADER;
        size_t amount = size - offset;
        if (amount > capacity) amount = capacity;
        packet[0] = DESK_PACKET_CHUNK;
        packet[1] = (uint8_t)offset;
        packet[2] = (uint8_t)(offset >> 8u);
        memcpy(packet + DESK_PACKET_CHUNK_HEADER, bytes + offset, amount);
        status = desk_radio_link_receive(&link, packet,
                                          DESK_PACKET_CHUNK_HEADER + amount,
                                          now_ms++, timeout_ms);
        if (status != DESK_RADIO_PROGRESS) {
            desk_radio_link_close(&link);
            return DESK_BAD_INPUT;
        }
        offset += amount;
    }

    const uint8_t commit = DESK_PACKET_COMMIT;
    status = desk_radio_link_receive(&link, &commit, 1u, now_ms++, timeout_ms);
    if (status != DESK_RADIO_QUEUED || !desk_dispatcher_process_one(&g_dispatcher) ||
        !g_transport_reply.valid ||
        g_transport_reply.channel != (desk_channel_t)channel ||
        g_transport_reply.peer_token != peer_token) {
        desk_radio_link_close(&link);
        return DESK_BAD_INPUT;
    }

    desk_ack_t ack = {0};
    const int good =
        desk_wire_decode_ack(g_transport_reply.ack, DESK_ACK_SIZE, &ack) == DESK_WIRE_OK &&
        desk_radio_link_ack_complete(&link, peer_token,
                                     g_transport_reply.ack, DESK_ACK_SIZE);
    desk_radio_link_close(&link);
    return good ? (int)ack.result : DESK_BAD_INPUT;
}

int desk_sim_snapshot(desk_content_t *content, desk_content_t *displayed,
                      uint32_t *revision)
{
    if (!g_initialized || content == NULL || displayed == NULL || revision == NULL)
        return 0;
    *content = g_core.content;
    *displayed = g_displayed;
    *revision = g_core.revision;
    return 1;
}

const char *desk_sim_result_name(int code)
{
    return desk_result_name((desk_result_t)code);
}

/* Serialize a real binary ACK from C Desk Core state after an applied request. */
int desk_sim_encode_ack(uint32_t request_id, int result_code,
                        uint8_t *out, size_t capacity)
{
    if (!g_initialized || result_code < DESK_OK ||
        result_code > DESK_REVISION_EXHAUSTED) return 0;
    const desk_ack_t ack = {
        request_id,
        g_core.revision,
        (desk_result_t)result_code
    };
    return desk_wire_encode_ack(&ack, out, capacity) == DESK_WIRE_OK;
}
