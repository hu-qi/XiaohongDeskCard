#ifndef XIAOHONG_DESK_PACKET_H
#define XIAOHONG_DESK_PACKET_H

#include "desk_wire.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Desk BLE/SLE application-level fragmentation, independent of radio SDK:
 * BEGIN: [0xA1][frame_length u16 little-endian][full_frame CRC32 u32 LE]
 * CHUNK: [0xA2][offset u16 LE][1..N frame bytes]
 * COMMIT:[0xA3]
 *
 * The transport owns one session per authenticated client. It must call
 * desk_packet_reset on disconnect / timeout and must not share a session
 * across unauthenticated clients or transports.
 */
#define DESK_PACKET_BEGIN 0xA1u
#define DESK_PACKET_CHUNK 0xA2u
#define DESK_PACKET_COMMIT 0xA3u
#define DESK_PACKET_BEGIN_SIZE 7u
#define DESK_PACKET_CHUNK_HEADER 3u

typedef enum {
    DESK_PACKET_PROGRESS = 0,
    DESK_PACKET_READY = 1,
    DESK_PACKET_BAD_INPUT = 2,
    DESK_PACKET_BAD_STATE = 3,
    DESK_PACKET_BAD_CRC = 4,
    DESK_PACKET_BAD_FRAME = 5
} desk_packet_result_t;

typedef struct {
    desk_wire_rx_t rx;
    uint32_t last_activity_ms;
} desk_packet_session_t;

void desk_packet_reset(desk_packet_session_t *session);

/*
 * now_ms uses the device's monotonic uint32 millisecond clock, allowing
 * wraparound with unsigned subtraction. Any inactivity >= timeout_ms aborts
 * the incomplete session. timeout_ms must be nonzero and below 2^31.
 *
 * Returns READY and writes *command_out ONLY after successful COMMIT and
 * validation of BOTH frame CRC32 and complete Desk Wire command.
 * A rejected packet resets this session, never modifies command_out.
 */
desk_packet_result_t desk_packet_feed(desk_packet_session_t *session,
                                      const uint8_t *packet, size_t packet_size,
                                      uint32_t now_ms, uint32_t timeout_ms,
                                      desk_set_command_t *command_out);
#endif
