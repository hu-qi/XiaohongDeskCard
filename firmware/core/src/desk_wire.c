#include "desk_wire.h"

#include <string.h>

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

uint32_t desk_wire_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    if (data == NULL && size != 0u) return 0u;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xEDB88320) : 0u);
    }
    return ~crc;
}

static size_t textlen(const char *s, size_t limit)
{
    size_t n = 0;
    while (n < limit && s[n] != '\0') n++;
    return n;
}

static size_t write_field(uint8_t *out, const char *value, size_t cap)
{
    const size_t n = textlen(value, cap);
    out[0] = (uint8_t)n;
    memcpy(out + 1, value, n);
    return n + 1u;
}

static int read_field(const uint8_t *buf, size_t size, size_t *pos,
                      char *out, size_t cap)
{
    if (*pos >= size) return 0;
    const size_t n = buf[(*pos)++];
    if (n >= cap || n > size - *pos) return 0;
    memcpy(out, buf + *pos, n);
    out[n] = '\0';
    *pos += n;
    return 1;
}

desk_wire_result_t desk_wire_encode_set(const desk_set_command_t *command,
                                        uint8_t *out, size_t capacity, size_t *out_size)
{
    if (command == NULL || out == NULL || out_size == NULL ||
        command->request_id == 0u || !desk_content_valid(&command->content))
        return DESK_WIRE_INVALID;
    const desk_content_t *c = &command->content;
    const size_t payload_size = 4u + strlen(c->name) + strlen(c->title) +
                                strlen(c->organization) + strlen(c->theme);
    const size_t total = DESK_WIRE_HEADER_SIZE + payload_size;
    if (total > capacity || total > DESK_WIRE_MAX_FRAME) return DESK_WIRE_SIZE;
    out[0] = DESK_WIRE_MAGIC;
    out[1] = DESK_WIRE_VERSION;
    out[2] = DESK_WIRE_OP_SET;
    out[3] = 0u;
    write32(out + 4, command->request_id);
    write32(out + 8, command->expected_revision);
    out[12] = (uint8_t)payload_size;
    out[13] = (uint8_t)(payload_size >> 8);
    size_t p = DESK_WIRE_HEADER_SIZE;
    p += write_field(out + p, c->name, sizeof(c->name));
    p += write_field(out + p, c->title, sizeof(c->title));
    p += write_field(out + p, c->organization, sizeof(c->organization));
    p += write_field(out + p, c->theme, sizeof(c->theme));
    write32(out + 14, desk_wire_crc32(out + DESK_WIRE_HEADER_SIZE, payload_size));
    *out_size = p;
    return DESK_WIRE_OK;
}

desk_wire_result_t desk_wire_decode_set(const uint8_t *frame, size_t size,
                                        desk_set_command_t *out)
{
    if (frame == NULL || out == NULL || size < DESK_WIRE_HEADER_SIZE ||
        size > DESK_WIRE_MAX_FRAME) return DESK_WIRE_INVALID;
    if (frame[0] != DESK_WIRE_MAGIC || frame[1] != DESK_WIRE_VERSION ||
        frame[2] != DESK_WIRE_OP_SET || frame[3] != 0u) return DESK_WIRE_INVALID;
    const size_t payload_size = (size_t)frame[12] | ((size_t)frame[13] << 8);
    if (payload_size != size - DESK_WIRE_HEADER_SIZE) return DESK_WIRE_SIZE;
    if (desk_wire_crc32(frame + DESK_WIRE_HEADER_SIZE, payload_size) != read32(frame + 14))
        return DESK_WIRE_CRC;
    desk_set_command_t tmp = {0};
    tmp.request_id = read32(frame + 4);
    tmp.expected_revision = read32(frame + 8);
    size_t p = DESK_WIRE_HEADER_SIZE;
    if (!read_field(frame, size, &p, tmp.content.name, sizeof(tmp.content.name)) ||
        !read_field(frame, size, &p, tmp.content.title, sizeof(tmp.content.title)) ||
        !read_field(frame, size, &p, tmp.content.organization, sizeof(tmp.content.organization)) ||
        !read_field(frame, size, &p, tmp.content.theme, sizeof(tmp.content.theme)) ||
        p != size || tmp.request_id == 0u || !desk_content_valid(&tmp.content))
        return DESK_WIRE_INVALID;
    *out = tmp;
    return DESK_WIRE_OK;
}

void desk_wire_rx_reset(desk_wire_rx_t *rx)
{
    if (rx != NULL) memset(rx, 0, sizeof(*rx));
}

desk_wire_result_t desk_wire_rx_begin(desk_wire_rx_t *rx, size_t frame_size, uint32_t crc)
{
    if (rx == NULL || frame_size < DESK_WIRE_HEADER_SIZE ||
        frame_size > DESK_WIRE_MAX_FRAME) return DESK_WIRE_SIZE;
    desk_wire_rx_reset(rx);
    rx->expected_size = frame_size;
    rx->expected_crc = crc;
    rx->active = true;
    return DESK_WIRE_OK;
}

desk_wire_result_t desk_wire_rx_chunk(desk_wire_rx_t *rx, size_t offset,
                                      const uint8_t *data, size_t size)
{
    if (rx == NULL || !rx->active || data == NULL ||
        offset > rx->expected_size || size > rx->expected_size - offset)
        return DESK_WIRE_SIZE;
    /* Retransmitted bytes accepted only when identical; partial overlap rejected. */
    if (offset < rx->received_size) {
        if (size <= rx->received_size - offset &&
            memcmp(rx->data + offset, data, size) == 0) return DESK_WIRE_OK;
        return DESK_WIRE_SEQUENCE;
    }
    if (offset != rx->received_size) return DESK_WIRE_SEQUENCE;
    memcpy(rx->data + offset, data, size);
    rx->received_size += size;
    return DESK_WIRE_OK;
}

desk_wire_result_t desk_wire_rx_finish(desk_wire_rx_t *rx,
                                       const uint8_t **frame, size_t *size)
{
    if (rx == NULL || !rx->active || frame == NULL || size == NULL ||
        rx->received_size != rx->expected_size) return DESK_WIRE_SIZE;
    if (desk_wire_crc32(rx->data, rx->expected_size) != rx->expected_crc)
        return DESK_WIRE_CRC;
    *frame = rx->data;
    *size = rx->expected_size;
    rx->active = false;
    return DESK_WIRE_OK;
}

/* Business-level ACK; never infer APPLIED solely from BLE/SSAP write success. */
desk_wire_result_t desk_wire_encode_ack(const desk_ack_t *ack, uint8_t *out, size_t capacity)
{
    if (ack == NULL || out == NULL || ack->request_id == 0u ||
        ack->result < DESK_OK || ack->result > DESK_REVISION_EXHAUSTED)
        return DESK_WIRE_INVALID;
    if (capacity < DESK_ACK_SIZE) return DESK_WIRE_SIZE;
    out[0] = DESK_ACK_MAGIC;
    out[1] = DESK_WIRE_VERSION;
    out[2] = (uint8_t)ack->result;
    out[3] = 0u;
    write32(out + 4, ack->request_id);
    write32(out + 8, ack->revision);
    write32(out + 12, desk_wire_crc32(out, 12u));
    return DESK_WIRE_OK;
}

desk_wire_result_t desk_wire_decode_ack(const uint8_t *frame, size_t size, desk_ack_t *out)
{
    if (frame == NULL || out == NULL || size != DESK_ACK_SIZE)
        return DESK_WIRE_SIZE;
    if (frame[0] != DESK_ACK_MAGIC || frame[1] != DESK_WIRE_VERSION ||
        frame[3] != 0u || frame[2] > (uint8_t)DESK_REVISION_EXHAUSTED ||
        read32(frame + 4) == 0u)
        return DESK_WIRE_INVALID;
    if (read32(frame + 12) != desk_wire_crc32(frame, 12u))
        return DESK_WIRE_CRC;
    desk_ack_t decoded;
    decoded.request_id = read32(frame + 4);
    decoded.revision = read32(frame + 8);
    decoded.result = (desk_result_t)frame[2];
    *out = decoded;
    return DESK_WIRE_OK;
}
