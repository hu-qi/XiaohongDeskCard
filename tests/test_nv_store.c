#include "desk_nv_store.h"
#include "desk_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Fake NOR flash: 2 separate 256-byte erase areas, 1->0 programming only. */
enum { SLOT_SIZE = 256 };
typedef struct {
    uint8_t slot[DESK_NV_SLOT_COUNT][SLOT_SIZE];
    size_t remaining_program_bytes;
    int limited;
    int fail_read;
    int fail_erase;
} fake_flash_t;

static void blank(fake_flash_t *flash)
{
    memset(flash, 0xFF, sizeof(*flash));
    flash->limited = 0;
    flash->remaining_program_bytes = 0;
    flash->fail_read = 0;
    flash->fail_erase = 0;
}

static int fake_read(void *context, unsigned slot, size_t offset,
                     uint8_t *out, size_t size)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    if (flash->fail_read || slot >= DESK_NV_SLOT_COUNT ||
        offset > SLOT_SIZE || size > SLOT_SIZE - offset) return 0;
    memcpy(out, flash->slot[slot] + offset, size);
    return 1;
}

static int fake_erase(void *context, unsigned slot)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    if (flash->fail_erase || slot >= DESK_NV_SLOT_COUNT) return 0;
    memset(flash->slot[slot], 0xFF, SLOT_SIZE);
    return 1;
}

static int fake_program(void *context, unsigned slot, size_t offset,
                        const uint8_t *data, size_t size)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    if (slot >= DESK_NV_SLOT_COUNT || offset > SLOT_SIZE ||
        size > SLOT_SIZE - offset || offset % 4u != 0u || size % 4u != 0u)
        return 0;
    size_t count = size;
    if (flash->limited && flash->remaining_program_bytes < count)
        count = flash->remaining_program_bytes;

    for (size_t i = 0; i < count; ++i) {
        const uint8_t before = flash->slot[slot][offset + i];
        if ((before & data[i]) != data[i]) return 0;
        flash->slot[slot][offset + i] = before & data[i];
    }
    if (flash->limited) flash->remaining_program_bytes -= count;
    return count == size;
}

static desk_nv_io_t make_io(fake_flash_t *flash)
{
    desk_nv_io_t io = {fake_read, fake_erase, fake_program, flash};
    return io;
}

static desk_content_t content(const char *name)
{
    desk_content_t value = {0};
    strcpy(value.name, name);
    strcpy(value.title, "负责人");
    strcpy(value.organization, "OpenHarmony 社区");
    strcpy(value.theme, "classic");
    return value;
}

static int same(const desk_content_t *a, const desk_content_t *b)
{
    return strcmp(a->name, b->name) == 0 &&
           strcmp(a->title, b->title) == 0 &&
           strcmp(a->organization, b->organization) == 0 &&
           strcmp(a->theme, b->theme) == 0;
}

static void load_expect(desk_nv_store_t *store,
                        const desk_content_t *expected, uint32_t revision)
{
    desk_content_t found = {0};
    uint32_t actual = 0u;
    assert(desk_nv_load(store, &found, &actual) == 1);
    assert(actual == revision && same(expected, &found));
}

int main(void)
{
    fake_flash_t flash;
    blank(&flash);
    desk_nv_store_t store;
    assert(desk_nv_init(&store, make_io(&flash)));
    assert(store.active_slot == -1 && store.current_revision == 0u);
    desk_content_t out = {0};
    uint32_t rev = 0u;
    assert(desk_nv_load(&store, &out, &rev) == 0);

    desk_content_t alice = content("张三");
    desk_content_t bob = content("李四");
    desk_content_t carol = content("王五");
    assert(desk_nv_save(&store, &alice, 1u));
    assert(store.active_slot == 0);
    load_expect(&store, &alice, 1u);
    assert(desk_nv_save(&store, &alice, 1u)); /* idempotent; no erase */
    assert(!desk_nv_save(&store, &bob, 1u));
    assert(!desk_nv_save(&store, &bob, 3u));
    assert(desk_nv_save(&store, &bob, 2u));
    assert(store.active_slot == 1);
    load_expect(&store, &bob, 2u);
    assert(desk_nv_save(&store, &carol, 3u));
    assert(store.active_slot == 0);
    load_expect(&store, &carol, 3u);

    /* Boot recovers latest committed revision. */
    desk_nv_store_t reboot = {0};
    assert(desk_nv_init(&reboot, make_io(&flash)));
    load_expect(&reboot, &carol, 3u);

    /* Corrupt newer slot; previous revision 2 must be recovered. */
    flash.slot[0][24] ^= 0x20u;
    desk_nv_store_t recovery = {0};
    assert(desk_nv_init(&recovery, make_io(&flash)));
    load_expect(&recovery, &bob, 2u);
    assert(recovery.active_slot == 1);
    /* A repair update writes to the damaged inactive slot only. */
    assert(desk_nv_save(&recovery, &carol, 3u));
    assert(desk_nv_init(&reboot, make_io(&flash)));
    load_expect(&reboot, &carol, 3u);

    /* Corrupt both committed slots: refuse silent overwrite. */
    fake_flash_t corrupt_both = flash;
    corrupt_both.slot[0][24] ^= 0x10u;
    corrupt_both.slot[1][24] ^= 0x10u;
    desk_nv_store_t bad = {0};
    assert(!desk_nv_init(&bad, make_io(&corrupt_both)));
    assert(!desk_nv_save(&bad, &alice, 4u));
    corrupt_both.fail_read = 1;
    assert(!desk_nv_init(&bad, make_io(&corrupt_both)));

    /* Crash injection at every byte of the body/final commit. The older
     * committed copy is never erased and must remain a valid recovery. */
    const fake_flash_t baseline = flash;
    for (size_t cut = 0u; cut < DESK_NV_RECORD_SIZE; ++cut) {
        fake_flash_t damaged = baseline;
        desk_nv_store_t live = {0};
        assert(desk_nv_init(&live, make_io(&damaged)));
        damaged.limited = 1;
        damaged.remaining_program_bytes = cut;
        assert(!desk_nv_save(&live, &alice, 4u));
        damaged.limited = 0;

        desk_nv_store_t after_crash = {0};
        assert(desk_nv_init(&after_crash, make_io(&damaged)));
        /* A partial last 4-byte write is not a valid COMMIT by assumption.
         * If the target HAL does not guarantee atomic 4-byte commit, it must
         * provide an equivalent atomic final-record primitive. */
        load_expect(&after_crash, &carol, 3u);
    }

    /* When all bytes can be programmed, commit is verified, then revision 4. */
    fake_flash_t complete = baseline;
    desk_nv_store_t live = {0};
    assert(desk_nv_init(&live, make_io(&complete)));
    complete.limited = 1;
    complete.remaining_program_bytes = DESK_NV_RECORD_SIZE;
    assert(desk_nv_save(&live, &alice, 4u));
    complete.limited = 0;
    assert(desk_nv_init(&reboot, make_io(&complete)));
    load_expect(&reboot, &alice, 4u);

    /* Simulated I/O failure before erasing the inactive slot. */
    fake_flash_t erase_failed = baseline;
    assert(desk_nv_init(&live, make_io(&erase_failed)));
    erase_failed.fail_erase = 1;
    assert(!desk_nv_save(&live, &alice, 4u));
    erase_failed.fail_erase = 0;
    assert(desk_nv_init(&reboot, make_io(&erase_failed)));
    load_expect(&reboot, &carol, 3u);

    /* Ensure the stored record uses explicit, portable byte layout. */
    assert(sizeof(desk_content_t) == 228u);
    assert(DESK_NV_BODY_SIZE == 244u);
    assert(DESK_NV_RECORD_SIZE == 248u);
    printf("PASS: two-slot NV, 248 power-cut positions, CRC fallback, recovery, revision\n");
    return 0;
}
