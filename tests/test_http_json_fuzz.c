/* Deterministic stress tests for the untrusted Wi-Fi JSON request boundary. */
#include "desk_http_json.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t rng_state = UINT32_C(0x91E10DA5);
static uint32_t random_u32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static const char valid[] =
    "{\"requestId\":17,\"expectedRevision\":0,"
    "\"content\":{\"name\":\"张三\",\"title\":\"开发者\","
    "\"organization\":\"OpenHarmony\",\"theme\":\"classic\"}}";

int main(void)
{
    uint8_t bytes[DESK_HTTP_MAX_BODY + 1u];
    unsigned accepted = 0u;
    for (unsigned trial = 0u; trial < 30000u; ++trial) {
        const size_t valid_size = sizeof(valid) - 1u;
        size_t size = valid_size;
        memcpy(bytes, valid, valid_size);
        const unsigned mode = trial % 5u;
        if (mode == 0u) {
            /* Random binary garbage, including NULs and invalid UTF-8. */
            size = 1u + (random_u32() % DESK_HTTP_MAX_BODY);
            for (size_t i = 0u; i < size; ++i)
                bytes[i] = (uint8_t)random_u32();
        } else if (mode == 1u) {
            /* Mutate up to 6 bytes within a plausible command. */
            const unsigned edits = 1u + random_u32() % 6u;
            for (unsigned edit = 0u; edit < edits; ++edit)
                bytes[random_u32() % size] = (uint8_t)random_u32();
        } else if (mode == 2u) {
            /* Truncation at arbitrary positions. */
            size = random_u32() % valid_size;
        } else if (mode == 3u) {
            /* A malformed JSON tail. */
            size += 1u + random_u32() % 12u;
            for (size_t i = valid_size; i < size; ++i)
                bytes[i] = (uint8_t)random_u32();
        } else {
            /* Untouched valid JSON must always remain accepted. */
        }

        desk_set_command_t out = {.request_id = UINT32_C(0xDEADBEEF)};
        const bool ok = desk_http_decode_set(bytes, size, &out);
        if (ok) {
            assert(out.request_id != 0u && desk_content_valid(&out.content));
            accepted++;
        } else {
            /* Decoder is transactional: rejection doesn't change command. */
            assert(out.request_id == UINT32_C(0xDEADBEEF));
        }
        if (mode == 4u) {
            assert(ok && out.request_id == 17u);
        }
    }
    /* Maximum body + 1 must never be accessed or accepted. */
    desk_set_command_t out = {.request_id = 33u};
    assert(!desk_http_decode_set(bytes, DESK_HTTP_MAX_BODY + 1u, &out));
    assert(out.request_id == 33u);
    printf("PASS: 30000 deterministic malformed/valid Wi-Fi JSON inputs; %u accepted\n",
           accepted);
    return 0;
}
