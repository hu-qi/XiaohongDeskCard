#include "desk_core.h"
#include "desk_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    desk_content_t stored;
    uint32_t stored_revision;
    unsigned saves;
    unsigned renders;
    int fail_save;
    int fail_render;
} fake_t;

static int load(void *user, desk_content_t *out, uint32_t *revision)
{
    fake_t *f = (fake_t *)user;
    if (!f->stored_revision) return 0;
    *out = f->stored;
    *revision = f->stored_revision;
    return 1;
}
static int save(void *user, const desk_content_t *value, uint32_t revision)
{
    fake_t *f = (fake_t *)user;
    if (f->fail_save) return 0;
    f->stored = *value;
    f->stored_revision = revision;
    f->saves++;
    return 1;
}
static int render(void *user, const desk_content_t *value)
{
    fake_t *f = (fake_t *)user;
    (void)value;
    f->renders++;
    return !f->fail_render;
}
static desk_set_command_t command(uint32_t id, uint32_t revision)
{
    desk_set_command_t cmd = {0};
    cmd.request_id = id;
    cmd.expected_revision = revision;
    (void)strcpy(cmd.content.name, "张三");
    (void)strcpy(cmd.content.title, "开发者");
    (void)strcpy(cmd.content.organization, "OpenHarmony 社区");
    (void)strcpy(cmd.content.theme, "classic");
    return cmd;
}

int main(void)
{
    fake_t f = {0};
    desk_hooks_t hooks = {load, save, render, &f};
    desk_core_t core;
    desk_core_init(&core, hooks);
    assert(core.revision == 0u);
    /* A fresh device must not render an empty header band: the default
     * organization is what puts text in it before the first SET. */
    assert(core.content.organization[0] != '\0');
    assert(desk_core_repaint(&core) == DESK_OK);
    desk_set_command_t cmd = command(42, 0);
    assert(desk_core_apply(&core, &cmd, false) == DESK_UNAUTHORIZED);
    assert(core.revision == 0u);
    assert(desk_core_apply(&core, &cmd, true) == DESK_OK);
    assert(core.revision == 1u && f.saves == 1u);
    assert(desk_core_apply(&core, &cmd, true) == DESK_OK); /* idempotent retry */
    assert(f.saves == 1u);
    cmd.expected_revision = 1u;
    assert(desk_core_apply(&core, &cmd, true) == DESK_DUPLICATE_ID);
    cmd.expected_revision = 0u;
    (void)strcpy(cmd.content.name, "Alice");
    assert(desk_core_apply(&core, &cmd, true) == DESK_DUPLICATE_ID);
    cmd = command(43, 0);
    assert(desk_core_apply(&core, &cmd, true) == DESK_CONFLICT);
    cmd.expected_revision = 1;
    cmd.content.name[0] = '\xFF';
    assert(desk_core_apply(&core, &cmd, true) == DESK_BAD_INPUT);
    cmd = command(43, 1);
    f.fail_save = 1;
    assert(desk_core_apply(&core, &cmd, true) == DESK_STORE_ERROR);
    assert(core.revision == 1u);
    f.fail_save = 0;
    f.fail_render = 1;
    assert(desk_core_apply(&core, &cmd, true) == DESK_RENDER_ERROR);
    assert(core.revision == 2u && f.stored_revision == 2u);
    f.fail_render = 0;
    /* Repeat the original request: recover the display without a second flash write. */
    const unsigned saved_before_retry = f.saves;
    assert(desk_core_apply(&core, &cmd, true) == DESK_OK);
    assert(f.saves == saved_before_retry);
    assert(desk_core_repaint(&core) == DESK_OK);

    desk_core_t restarted;
    desk_core_init(&restarted, hooks);
    assert(restarted.revision == 2u);
    assert(strcmp(restarted.content.name, "张三") == 0);

    /* Same binary command is suitable for BLE and SLE; Wi-Fi can map JSON to it. */
    desk_set_command_t tx = command(77, 2);
    uint8_t bytes[DESK_WIRE_MAX_FRAME];
    size_t length = 0;
    assert(desk_wire_encode_set(&tx, bytes, sizeof(bytes), &length) == DESK_WIRE_OK);
    desk_set_command_t decoded = {0};
    assert(desk_wire_decode_set(bytes, length, &decoded) == DESK_WIRE_OK);
    assert(strcmp(decoded.content.organization, "OpenHarmony 社区") == 0);
    assert(decoded.request_id == 77u && decoded.expected_revision == 2u);

    desk_wire_rx_t rx = {0};
    assert(desk_wire_rx_begin(&rx, length, desk_wire_crc32(bytes, length)) == DESK_WIRE_OK);
    size_t first = 15u;
    assert(desk_wire_rx_chunk(&rx, 0, bytes, first) == DESK_WIRE_OK);
    assert(desk_wire_rx_chunk(&rx, 0, bytes, first) == DESK_WIRE_OK); /* retry */
    assert(desk_wire_rx_chunk(&rx, first + 1, bytes + first + 1, 2) == DESK_WIRE_SEQUENCE);
    assert(desk_wire_rx_chunk(&rx, first, bytes + first, length - first) == DESK_WIRE_OK);
    const uint8_t *complete = NULL;
    size_t complete_size = 0;
    assert(desk_wire_rx_finish(&rx, &complete, &complete_size) == DESK_WIRE_OK);
    assert(complete_size == length && desk_wire_decode_set(complete, complete_size, &decoded) == DESK_WIRE_OK);
    assert(desk_core_apply(&restarted, &decoded, true) == DESK_OK);
    assert(restarted.revision == 3u);

    /* Business ACK is separate from radio write completion. */
    desk_ack_t ack = {77u, restarted.revision, DESK_OK};
    uint8_t ack_frame[DESK_ACK_SIZE] = {0};
    desk_ack_t ack_out = {0};
    assert(desk_wire_encode_ack(&ack, ack_frame, sizeof(ack_frame)) == DESK_WIRE_OK);
    assert(ack_frame[0] == DESK_ACK_MAGIC && ack_frame[1] == DESK_WIRE_VERSION);
    assert(desk_wire_decode_ack(ack_frame, sizeof(ack_frame), &ack_out) == DESK_WIRE_OK);
    assert(ack_out.request_id == 77u && ack_out.revision == 3u &&
           ack_out.result == DESK_OK);
    assert(desk_wire_decode_ack(ack_frame, DESK_ACK_SIZE - 1u, &ack_out) ==
           DESK_WIRE_SIZE);
    ack_frame[11] ^= 0x01u;
    assert(desk_wire_decode_ack(ack_frame, sizeof(ack_frame), &ack_out) == DESK_WIRE_CRC);
    ack_frame[11] ^= 0x01u;
    ack_frame[2] = 0xFFu;
    assert(desk_wire_decode_ack(ack_frame, sizeof(ack_frame), &ack_out) ==
           DESK_WIRE_INVALID);
    ack_frame[2] = DESK_OK;
    ack.request_id = 0u;
    assert(desk_wire_encode_ack(&ack, ack_frame, sizeof(ack_frame)) ==
           DESK_WIRE_INVALID);

    bytes[18] ^= 0x40;
    assert(desk_wire_decode_set(bytes, length, &decoded) == DESK_WIRE_CRC);
    assert(desk_wire_decode_set(bytes, length - 1, &decoded) == DESK_WIRE_SIZE);
    printf("PASS: core/persist/UTF8/authorization/idempotency/revision/wire/fragment/ACK/CRC\n");
    return 0;
}
