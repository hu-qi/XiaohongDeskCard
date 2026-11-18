#include "desk_nv_store.h"
#include "desk_wire.h"

#include <limits.h>
#include <string.h>

#define NV_FIELD_OFFSET 12u
#define NV_CRC_OFFSET 240u
#define NV_MAGIC_0 0x58u
#define NV_MAGIC_1 0x48u
#define NV_MAGIC_2 0x44u
#define NV_MAGIC_3 0x4Eu
#define NV_COMMIT_MARKER UINT32_C(0xC01117ED)

typedef enum { NV_EMPTY = 0, NV_VALID = 1, NV_CORRUPT = 2, NV_IO_ERROR = 3 } slot_state_t;

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static size_t field_len(const char *s, size_t size)
{
    size_t i = 0;
    while (i < size && s[i] != '\0') i++;
    return i;
}

static bool canonical_field(const char *s, size_t size)
{
    const size_t length = field_len(s, size);
    if (length >= size) return false;
    for (size_t i = length + 1u; i < size; i++)
        if (s[i] != '\0') return false;
    return true;
}

static void encode_field(uint8_t *target, const char *field, size_t capacity)
{
    const size_t n = field_len(field, capacity);
    memset(target, 0, capacity);
    memcpy(target, field, n);
}

static bool valid_payload(const desk_content_t *content)
{
    return desk_content_valid(content) &&
           canonical_field(content->name, sizeof(content->name)) &&
           canonical_field(content->title, sizeof(content->title)) &&
           canonical_field(content->organization, sizeof(content->organization)) &&
           canonical_field(content->theme, sizeof(content->theme));
}

static bool same_content(const desk_content_t *a, const desk_content_t *b)
{
    return strcmp(a->name, b->name) == 0 &&
           strcmp(a->title, b->title) == 0 &&
           strcmp(a->organization, b->organization) == 0 &&
           strcmp(a->theme, b->theme) == 0;
}

static slot_state_t read_slot(desk_nv_store_t *store, unsigned slot,
                             desk_content_t *content, uint32_t *revision)
{
    uint8_t bytes[DESK_NV_RECORD_SIZE];
    if (!store->io.read(store->io.ctx, slot, 0u, bytes, sizeof(bytes)))
        return NV_IO_ERROR;

    bool erased = true;
    for (size_t i = 0; i < sizeof(bytes); i++) {
        if (bytes[i] != 0xFFu) {
            erased = false;
            break;
        }
    }
    if (erased) return NV_EMPTY;

    if (bytes[0] != NV_MAGIC_0 || bytes[1] != NV_MAGIC_1 ||
        bytes[2] != NV_MAGIC_2 || bytes[3] != NV_MAGIC_3 ||
        read16(bytes + 4) != DESK_NV_SCHEMA ||
        read16(bytes + 6) != sizeof(desk_content_t) ||
        read32(bytes + 8) == 0u ||
        read32(bytes + DESK_NV_COMMIT_OFFSET) != NV_COMMIT_MARKER ||
        read32(bytes + NV_CRC_OFFSET) != desk_wire_crc32(bytes, NV_CRC_OFFSET))
        return NV_CORRUPT;

    desk_content_t temp = {0};
    memcpy(&temp, bytes + NV_FIELD_OFFSET, sizeof(temp));
    if (!valid_payload(&temp)) return NV_CORRUPT;

    *content = temp;
    *revision = read32(bytes + 8);
    return NV_VALID;
}

bool desk_nv_init(desk_nv_store_t *store, desk_nv_io_t io)
{
    if (store == NULL || io.read == NULL || io.erase == NULL ||
        io.program == NULL) return false;

    memset(store, 0, sizeof(*store));
    store->io = io;
    store->active_slot = -1;
    desk_content_t first = {0}, second = {0};
    uint32_t rev0 = 0u, rev1 = 0u;
    const slot_state_t a = read_slot(store, 0u, &first, &rev0);
    const slot_state_t b = read_slot(store, 1u, &second, &rev1);
    if (a == NV_IO_ERROR || b == NV_IO_ERROR) return false;

    const bool v0 = a == NV_VALID, v1 = b == NV_VALID;
    if (!v0 && !v1) {
        /* Do not silently overwrite corrupted nonempty storage on boot. */
        if (a == NV_CORRUPT || b == NV_CORRUPT) return false;
        store->ready = true; /* first boot: both sectors erased */
        return true;
    }

    if (v0 && v1 && rev0 == rev1 && !same_content(&first, &second))
        return false; /* inconsistent committed copies */
    const unsigned selected = v1 && (!v0 || rev1 > rev0) ? 1u : 0u;
    store->active_slot = (int)selected;
    store->current_revision = selected == 0u ? rev0 : rev1;
    store->current_content = selected == 0u ? first : second;
    store->ready = true;
    return true;
}

int desk_nv_load(void *user, desk_content_t *content, uint32_t *revision)
{
    const desk_nv_store_t *store = (const desk_nv_store_t *)user;
    if (store == NULL || !store->ready || store->active_slot < 0 ||
        content == NULL || revision == NULL) return 0;
    *content = store->current_content;
    *revision = store->current_revision;
    return 1;
}

int desk_nv_save(void *user, const desk_content_t *content, uint32_t revision)
{
    desk_nv_store_t *store = (desk_nv_store_t *)user;
    if (store == NULL || !store->ready || content == NULL ||
        !desk_content_valid(content) || revision == 0u) return 0;

    /* If the final marker was committed but the previous readback failed,
     * callers may retry the same revision. Re-scan only before that retry
     * is attempted by the next boot; no automatic flash re-erase is needed. */
    if (store->active_slot >= 0 && revision == store->current_revision)
        return same_content(content, &store->current_content) ? 1 : 0;
    if (store->current_revision == UINT32_MAX ||
        revision != store->current_revision + 1u) return 0;

    const unsigned target = store->active_slot == 0 ? 1u : 0u;
    uint8_t bytes[DESK_NV_RECORD_SIZE];
    memset(bytes, 0xFF, sizeof(bytes));
    bytes[0] = NV_MAGIC_0; bytes[1] = NV_MAGIC_1;
    bytes[2] = NV_MAGIC_2; bytes[3] = NV_MAGIC_3;
    write16(bytes + 4, DESK_NV_SCHEMA);
    write16(bytes + 6, (uint16_t)sizeof(desk_content_t));
    write32(bytes + 8, revision);

    desk_content_t canonical = {0};
    encode_field((uint8_t *)canonical.name, content->name, sizeof(canonical.name));
    encode_field((uint8_t *)canonical.title, content->title, sizeof(canonical.title));
    encode_field((uint8_t *)canonical.organization, content->organization,
                 sizeof(canonical.organization));
    encode_field((uint8_t *)canonical.theme, content->theme, sizeof(canonical.theme));
    memcpy(bytes + NV_FIELD_OFFSET, &canonical, sizeof(canonical));
    write32(bytes + NV_CRC_OFFSET, desk_wire_crc32(bytes, NV_CRC_OFFSET));

    /* Inactive slot only, then data readback, then a distinct final commit. */
    if (!store->io.erase(store->io.ctx, target) ||
        !store->io.program(store->io.ctx, target, 0u, bytes, DESK_NV_BODY_SIZE))
        return 0;
    uint8_t check[DESK_NV_RECORD_SIZE];
    if (!store->io.read(store->io.ctx, target, 0u, check, sizeof(check)) ||
        memcmp(check, bytes, DESK_NV_BODY_SIZE) != 0 ||
        check[DESK_NV_COMMIT_OFFSET] != 0xFFu ||
        check[DESK_NV_COMMIT_OFFSET + 1u] != 0xFFu ||
        check[DESK_NV_COMMIT_OFFSET + 2u] != 0xFFu ||
        check[DESK_NV_COMMIT_OFFSET + 3u] != 0xFFu)
        return 0;

    uint8_t marker[4];
    write32(marker, NV_COMMIT_MARKER);
    if (!store->io.program(store->io.ctx, target, DESK_NV_COMMIT_OFFSET,
                           marker, sizeof(marker)))
        return 0;

    desk_content_t confirmed = {0};
    uint32_t confirmed_revision = 0u;
    if (read_slot(store, target, &confirmed, &confirmed_revision) != NV_VALID ||
        confirmed_revision != revision || !same_content(&canonical, &confirmed))
        return 0;

    store->current_content = canonical;
    store->current_revision = revision;
    store->active_slot = (int)target;
    return 1;
}
