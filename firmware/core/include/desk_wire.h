#ifndef XIAOHONG_DESK_WIRE_H
#define XIAOHONG_DESK_WIRE_H

#include "desk_core.h"
#include <stddef.h>
#include <stdint.h>

#define DESK_WIRE_MAGIC 0xD5u
#define DESK_WIRE_VERSION 1u
#define DESK_WIRE_OP_SET 1u
#define DESK_WIRE_HEADER_SIZE 18u
#define DESK_WIRE_MAX_FRAME 384u
#define DESK_ACK_MAGIC 0xD6u
#define DESK_ACK_SIZE 16u

/*
 * Wire ACK v1 (BLE/SLE business result, NOT transport write confirmation):
 * [magic D6][version 01][desk_result_t code][reserved 00]
 * [request_id: u32LE][revision: u32LE][crc32(first 12 bytes): u32LE]
 */
typedef struct {
    uint32_t request_id;
    uint32_t revision;
    desk_result_t result;
} desk_ack_t;

typedef enum {
    DESK_WIRE_OK = 0,
    DESK_WIRE_INVALID,
    DESK_WIRE_SIZE,
    DESK_WIRE_CRC,
    DESK_WIRE_SEQUENCE
} desk_wire_result_t;

/* Binary wire format for BLE/SLE; Wi-Fi HTTP may map JSON to desk_set_command_t. */
desk_wire_result_t desk_wire_encode_set(const desk_set_command_t *command,
                                        uint8_t *out, size_t capacity, size_t *out_size);
desk_wire_result_t desk_wire_decode_set(const uint8_t *frame, size_t size,
                                        desk_set_command_t *out);
uint32_t desk_wire_crc32(const uint8_t *data, size_t size);
desk_wire_result_t desk_wire_encode_ack(const desk_ack_t *ack, uint8_t *out, size_t capacity);
desk_wire_result_t desk_wire_decode_ack(const uint8_t *frame, size_t size, desk_ack_t *out);

/* Transport-agnostic fragmentation accumulator: BEGIN, CHUNK(offset), FINISH. */
typedef struct {
    uint8_t data[DESK_WIRE_MAX_FRAME];
    size_t expected_size;
    size_t received_size;
    uint32_t expected_crc;
    bool active;
} desk_wire_rx_t;

desk_wire_result_t desk_wire_rx_begin(desk_wire_rx_t *rx, size_t frame_size, uint32_t crc);
desk_wire_result_t desk_wire_rx_chunk(desk_wire_rx_t *rx, size_t offset,
                                      const uint8_t *data, size_t size);
desk_wire_result_t desk_wire_rx_finish(desk_wire_rx_t *rx,
                                       const uint8_t **frame, size_t *size);
void desk_wire_rx_reset(desk_wire_rx_t *rx);

#endif
