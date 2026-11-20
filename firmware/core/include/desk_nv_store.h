#ifndef XIAOHONG_DESK_NV_STORE_H
#define XIAOHONG_DESK_NV_STORE_H

#include "desk_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Portable flash/NV two-slot journal. No SDK dependency, heap allocation or
 * compiler-dependent packed struct. Every record is exactly 248 bytes.
 *
 * Offsets (little-endian):
 * 0: magic "XHDN" (4), 4: schema u16 (1), 6: payload u16 (228)
 * 8: revision u32, 12: fixed UTF-8 fields (228), 240: CRC32 (4),
 * 244: commit marker u32 (4), PROGRAMMED LAST.
 *
 * The HAL MUST provide two independent erase regions, erased byte 0xFF,
 * no changes to the currently active slot when writing the inactive slot,
 * and power-fail-atomic/aligned 4-byte commit marker programming.
 * These requirements must be checked against actual WS63 NV/Flash SDK.
 */
#define DESK_NV_SLOT_COUNT 2u
#define DESK_NV_RECORD_SIZE 248u
#define DESK_NV_BODY_SIZE 244u
#define DESK_NV_COMMIT_OFFSET 244u
#define DESK_NV_SCHEMA 1u

typedef struct {
    /* Return 1 for success; 0 for I/O error. slot is 0 or 1. */
    int (*read)(void *ctx, unsigned slot, size_t offset, uint8_t *data, size_t size);
    int (*erase)(void *ctx, unsigned slot);
    /* Must be safe for aligned writes; 0 on incomplete/failed write. */
    int (*program)(void *ctx, unsigned slot, size_t offset,
                   const uint8_t *data, size_t size);
    void *ctx;
} desk_nv_io_t;

typedef struct {
    desk_nv_io_t io;
    desk_content_t current_content;
    uint32_t current_revision;
    int active_slot; /* -1 when both sectors erased, otherwise 0 or 1. */
    bool ready;
} desk_nv_store_t;

/*
 * Returns false on unreadable storage, two conflicting records with identical
 * revision, or corrupted nonempty storage when no valid slot survives.
 * Caller must NOT enable editing if init fails.
 */
bool desk_nv_init(desk_nv_store_t *store, desk_nv_io_t io);

/* Desk Core hooks. user points to desk_nv_store_t; only the UI task calls. */
int desk_nv_load(void *user, desk_content_t *content, uint32_t *revision);
int desk_nv_save(void *user, const desk_content_t *content, uint32_t revision);

#endif
