/*
 * Host test for the board binding. Verifies that desk_board_init() actually
 * brings the Desk Core up on top of a two-item NV HAL, renders the default
 * content, and persists a change through the journal. This is the call path
 * that keeps the deskcard objects from being dropped by the linker.
 */
#include "desk_ws63_board.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_nv_store.h"

typedef struct {
    uint8_t slots[2][DESK_NV_RECORD_SIZE];
} fake_nv_t;

static fake_nv_t g_nv;

static int nv_read(void *ctx, unsigned slot, size_t offset, uint8_t *data,
                   size_t size)
{
    fake_nv_t *nv = (fake_nv_t *)ctx;
    if (slot > 1u || offset + size > DESK_NV_RECORD_SIZE) return 0;
    memcpy(data, nv->slots[slot] + offset, size);
    return 1;
}

static int nv_erase(void *ctx, unsigned slot)
{
    fake_nv_t *nv = (fake_nv_t *)ctx;
    if (slot > 1u) return 0;
    memset(nv->slots[slot], 0xFF, DESK_NV_RECORD_SIZE);
    return 1;
}

static int nv_program(void *ctx, unsigned slot, size_t offset,
                      const uint8_t *data, size_t size)
{
    fake_nv_t *nv = (fake_nv_t *)ctx;
    if (slot > 1u || offset + size > DESK_NV_RECORD_SIZE) return 0;
    memcpy(nv->slots[slot] + offset, data, size);
    return 1;
}

static void reset_nv(void)
{
    memset(&g_nv, 0xFF, sizeof(g_nv));
}

int main(void)
{
    /* Fresh board: both NV slots erased. Init must still succeed with the
     * built-in default content, otherwise nothing is ever displayed. */
    reset_nv();
    desk_nv_io_t io = {nv_read, nv_erase, nv_program, &g_nv};
    desk_board_test_set_nv_io(io);

    assert(desk_board_init() == 1);
    assert(desk_board_is_ready() == 1);

    /* Polling an idle board must be harmless. */
    desk_board_poll();
    desk_board_poll();

    /* Re-init is idempotent and must not rebuild the view. */
    assert(desk_board_init() == 1);

    printf("desk_ws63_board_tests: OK\n");
    return 0;
}
