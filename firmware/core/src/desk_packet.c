#include "desk_packet.h"
#include <string.h>

static uint16_t read16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void desk_packet_reset(desk_packet_session_t *session)
{
    if (session == NULL) return;
    desk_wire_rx_reset(&session->rx);
    session->last_activity_ms = 0u;
}

static desk_packet_result_t abort_packet(desk_packet_session_t *session,
                                          desk_packet_result_t reason)
{
    desk_packet_reset(session);
    return reason;
}

desk_packet_result_t desk_packet_feed(desk_packet_session_t *session,
                                      const uint8_t *packet, size_t packet_size,
                                      uint32_t now_ms, uint32_t timeout_ms,
                                      desk_set_command_t *command_out)
{
    if (session == NULL || command_out == NULL || packet == NULL ||
        packet_size == 0u || timeout_ms == 0u ||
        timeout_ms >= UINT32_C(0x80000000))
        return DESK_PACKET_BAD_INPUT;

    if (session->rx.active &&
        (uint32_t)(now_ms - session->last_activity_ms) >= timeout_ms) {
        desk_packet_reset(session);
    }

    if (packet[0] == DESK_PACKET_BEGIN) {
        if (packet_size != DESK_PACKET_BEGIN_SIZE)
            return abort_packet(session, DESK_PACKET_BAD_INPUT);
        const size_t frame_length = read16(packet + 1u);
        const uint32_t expected_crc = read32(packet + 3u);
        if (desk_wire_rx_begin(&session->rx, frame_length, expected_crc) !=
            DESK_WIRE_OK)
            return abort_packet(session, DESK_PACKET_BAD_INPUT);
        session->last_activity_ms = now_ms;
        return DESK_PACKET_PROGRESS;
    }

    if (!session->rx.active)
        return DESK_PACKET_BAD_STATE;

    if (packet[0] == DESK_PACKET_CHUNK) {
        if (packet_size <= DESK_PACKET_CHUNK_HEADER)
            return abort_packet(session, DESK_PACKET_BAD_INPUT);
        const size_t offset = read16(packet + 1u);
        const desk_wire_result_t result = desk_wire_rx_chunk(
            &session->rx, offset, packet + DESK_PACKET_CHUNK_HEADER,
            packet_size - DESK_PACKET_CHUNK_HEADER);
        if (result != DESK_WIRE_OK)
            return abort_packet(session, DESK_PACKET_BAD_STATE);
        session->last_activity_ms = now_ms;
        return DESK_PACKET_PROGRESS;
    }

    if (packet[0] == DESK_PACKET_COMMIT) {
        if (packet_size != 1u)
            return abort_packet(session, DESK_PACKET_BAD_INPUT);
        const uint8_t *frame = NULL;
        size_t frame_size = 0u;
        const desk_wire_result_t finish = desk_wire_rx_finish(
            &session->rx, &frame, &frame_size);
        if (finish != DESK_WIRE_OK)
            return abort_packet(session,
                finish == DESK_WIRE_CRC ? DESK_PACKET_BAD_CRC : DESK_PACKET_BAD_STATE);
        desk_set_command_t result = {0};
        const desk_wire_result_t decoded = desk_wire_decode_set(
            frame, frame_size, &result);
        desk_packet_reset(session);
        if (decoded != DESK_WIRE_OK)
            return decoded == DESK_WIRE_CRC ? DESK_PACKET_BAD_CRC :
                   DESK_PACKET_BAD_FRAME;
        *command_out = result;
        return DESK_PACKET_READY;
    }

    return abort_packet(session, DESK_PACKET_BAD_INPUT);
}
