#include "desk_http_json.h"

#include <limits.h>
#include <string.h>

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t at;
} json_reader_t;

typedef struct {
    char *text;
    size_t capacity;
    size_t count;
    bool ok;
} json_writer_t;

static void skip_ws(json_reader_t *r)
{
    while (r->at < r->size) {
        const uint8_t c = r->bytes[r->at];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
        r->at++;
    }
}

static bool take(json_reader_t *r, uint8_t c)
{
    skip_ws(r);
    if (r->at >= r->size || r->bytes[r->at] != c) return false;
    r->at++;
    return true;
}

static bool byte_to(char *out, size_t capacity, size_t *length, uint8_t c)
{
    if (*length >= capacity - 1u) return false;
    out[(*length)++] = (char)c;
    return true;
}

static int hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_hex4(json_reader_t *r, uint32_t *result)
{
    if (r->size - r->at < 4u) return false;
    uint32_t cp = 0u;
    for (unsigned i = 0u; i < 4u; ++i) {
        const int h = hex_digit(r->bytes[r->at++]);
        if (h < 0) return false;
        cp = (cp << 4u) | (uint32_t)h;
    }
    *result = cp;
    return true;
}

static bool write_codepoint(char *out, size_t capacity,
                            size_t *length, uint32_t cp)
{
    if (cp == 0u || cp < 0x20u || cp > 0x10FFFFu ||
        (cp >= 0xD800u && cp <= 0xDFFFu)) return false;
    if (cp < 0x80u) {
        return byte_to(out, capacity, length, (uint8_t)cp);
    }
    if (cp < 0x800u) {
        return byte_to(out, capacity, length, (uint8_t)(0xC0u | (cp >> 6u))) &&
               byte_to(out, capacity, length, (uint8_t)(0x80u | (cp & 0x3Fu)));
    }
    if (cp < 0x10000u) {
        return byte_to(out, capacity, length, (uint8_t)(0xE0u | (cp >> 12u))) &&
               byte_to(out, capacity, length, (uint8_t)(0x80u | ((cp >> 6u) & 0x3Fu))) &&
               byte_to(out, capacity, length, (uint8_t)(0x80u | (cp & 0x3Fu)));
    }
    return byte_to(out, capacity, length, (uint8_t)(0xF0u | (cp >> 18u))) &&
           byte_to(out, capacity, length, (uint8_t)(0x80u | ((cp >> 12u) & 0x3Fu))) &&
           byte_to(out, capacity, length, (uint8_t)(0x80u | ((cp >> 6u) & 0x3Fu))) &&
           byte_to(out, capacity, length, (uint8_t)(0x80u | (cp & 0x3Fu)));
}

/* Writes only a bounded local buffer. Embedded NUL/control strings rejected. */
static bool read_string(json_reader_t *r, char *out, size_t capacity)
{
    if (out == NULL || capacity == 0u || !take(r, '"')) return false;
    size_t length = 0u;
    while (r->at < r->size) {
        uint8_t c = r->bytes[r->at++];
        if (c == '"') {
            out[length] = '\0';
            return true;
        }
        if (c < 0x20u) return false;
        if (c != '\\') {
            if (!byte_to(out, capacity, &length, c)) return false;
            continue;
        }
        if (r->at >= r->size) return false;
        const uint8_t escape = r->bytes[r->at++];
        if (escape == '"' || escape == '\\' || escape == '/') {
            if (!byte_to(out, capacity, &length, escape)) return false;
            continue;
        }
        if (escape != 'u') return false; /* No escaped controls in desk fields. */
        uint32_t cp = 0u;
        if (!read_hex4(r, &cp)) return false;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            if (r->size - r->at < 6u ||
                r->bytes[r->at] != '\\' || r->bytes[r->at + 1u] != 'u')
                return false;
            r->at += 2u;
            uint32_t low = 0u;
            if (!read_hex4(r, &low) || low < 0xDC00u || low > 0xDFFFu)
                return false;
            cp = UINT32_C(0x10000) +
                 ((cp - UINT32_C(0xD800)) << 10u) +
                 (low - UINT32_C(0xDC00));
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return false;
        }
        if (!write_codepoint(out, capacity, &length, cp)) return false;
    }
    return false;
}

static bool read_u32(json_reader_t *r, uint32_t *value)
{
    skip_ws(r);
    if (r->at >= r->size) return false;
    const size_t start = r->at;
    uint32_t result = 0u;
    while (r->at < r->size) {
        uint8_t c = r->bytes[r->at];
        if (c < '0' || c > '9') break;
        if (r->at > start && r->bytes[start] == '0') return false;
        uint32_t digit = (uint32_t)(c - '0');
        if (result > (UINT32_MAX - digit) / 10u) return false;
        result = result * 10u + digit;
        r->at++;
    }
    if (r->at == start) return false;
    *value = result;
    return true;
}

static bool read_content(json_reader_t *r, desk_content_t *content)
{
    if (!take(r, '{')) return false;
    unsigned seen = 0u;
    for (;;) {
        char key[32] = {0};
        if (!read_string(r, key, sizeof(key)) || !take(r, ':')) return false;
        unsigned bit = 0u;
        char *field = NULL;
        size_t cap = 0u;
        if (strcmp(key, "name") == 0) {
            bit = 1u; field = content->name; cap = sizeof(content->name);
        } else if (strcmp(key, "title") == 0) {
            bit = 2u; field = content->title; cap = sizeof(content->title);
        } else if (strcmp(key, "organization") == 0) {
            bit = 4u; field = content->organization; cap = sizeof(content->organization);
        } else if (strcmp(key, "theme") == 0) {
            bit = 8u; field = content->theme; cap = sizeof(content->theme);
        } else {
            return false;
        }
        if ((seen & bit) != 0u || !read_string(r, field, cap)) return false;
        seen |= bit;
        if (take(r, '}')) break;
        if (!take(r, ',')) return false;
    }
    return seen == 15u && desk_content_valid(content);
}

bool desk_http_decode_set(const uint8_t *body, size_t size,
                          desk_set_command_t *command)
{
    if (body == NULL || command == NULL || size == 0u ||
        size > DESK_HTTP_MAX_BODY) return false;
    json_reader_t r = {body, size, 0u};
    desk_set_command_t decoded = {0};
    if (!take(&r, '{')) return false;
    unsigned seen = 0u;
    for (;;) {
        char key[32] = {0};
        if (!read_string(&r, key, sizeof(key)) || !take(&r, ':')) return false;
        unsigned bit = 0u;
        if (strcmp(key, "requestId") == 0) {
            bit = 1u;
            if (!read_u32(&r, &decoded.request_id) ||
                decoded.request_id == 0u) return false;
        } else if (strcmp(key, "expectedRevision") == 0) {
            bit = 2u;
            if (!read_u32(&r, &decoded.expected_revision)) return false;
        } else if (strcmp(key, "content") == 0) {
            bit = 4u;
            if (!read_content(&r, &decoded.content)) return false;
        } else {
            return false;
        }
        if ((seen & bit) != 0u) return false;
        seen |= bit;
        if (take(&r, '}')) break;
        if (!take(&r, ',')) return false;
    }
    skip_ws(&r);
    if (seen != 7u || r.at != r.size) return false;
    *command = decoded;
    return true;
}

static void emit_byte(json_writer_t *w, char c)
{
    if (!w->ok) return;
    if (w->count + 1u >= w->capacity) {
        w->ok = false;
        return;
    }
    w->text[w->count++] = c;
}

static void emit_literal(json_writer_t *w, const char *text)
{
    while (*text != '\0') emit_byte(w, *text++);
}

static void emit_quoted(json_writer_t *w, const char *text)
{
    emit_byte(w, '"');
    for (const uint8_t *p = (const uint8_t *)text; *p != 0u; ++p) {
        if (*p == '"' || *p == '\\') {
            emit_byte(w, '\\');
            emit_byte(w, (char)*p);
        } else if (*p < 0x20u) {
            w->ok = false;
            return;
        } else {
            emit_byte(w, (char)*p);
        }
    }
    emit_byte(w, '"');
}

static void emit_uint(json_writer_t *w, uint32_t number)
{
    char digits[10];
    unsigned count = 0u;
    do {
        digits[count++] = (char)('0' + (number % 10u));
        number /= 10u;
    } while (number != 0u);
    while (count != 0u) emit_byte(w, digits[--count]);
}

static void emit_content(json_writer_t *w, const desk_content_t *content)
{
    emit_literal(w, "{\"name\":");
    emit_quoted(w, content->name);
    emit_literal(w, ",\"title\":");
    emit_quoted(w, content->title);
    emit_literal(w, ",\"organization\":");
    emit_quoted(w, content->organization);
    emit_literal(w, ",\"theme\":");
    emit_quoted(w, content->theme);
    emit_byte(w, '}');
}

static void emit_state(json_writer_t *w, const desk_http_state_t *state)
{
    emit_literal(w, "{\"revision\":");
    emit_uint(w, state->revision);
    emit_literal(w, ",\"content\":");
    emit_content(w, &state->content);
    emit_literal(w, ",\"display\":");
    emit_content(w, &state->display);
    emit_literal(w, ",\"device\":\"XiaohongDesk\",\"hardwareValidated\":");
    emit_literal(w, state->hardware_validated ? "true" : "false");
    emit_byte(w, '}');
}

static size_t complete(json_writer_t *w)
{
    if (!w->ok) return 0u;
    w->text[w->count] = '\0';
    return w->count;
}

size_t desk_http_encode_status(const desk_http_state_t *state,
                               char *out, size_t capacity)
{
    if (state == NULL || out == NULL || capacity == 0u ||
        !desk_content_valid(&state->content) ||
        !desk_content_valid(&state->display)) return 0u;
    json_writer_t w = {out, capacity, 0u, true};
    emit_state(&w, state);
    return complete(&w);
}

size_t desk_http_encode_reply(desk_result_t result,
                              const desk_http_state_t *state,
                              char *out, size_t capacity)
{
    if (state == NULL || out == NULL || capacity == 0u ||
        result < DESK_OK || result > DESK_REVISION_EXHAUSTED ||
        !desk_content_valid(&state->content) ||
        !desk_content_valid(&state->display)) return 0u;
    json_writer_t w = {out, capacity, 0u, true};
    emit_literal(&w, "{\"result\":");
    emit_quoted(&w, desk_result_name(result));
    emit_literal(&w, ",\"transport\":\"WIFI_HTTP\",\"state\":");
    emit_state(&w, state);
    emit_byte(&w, '}');
    return complete(&w);
}
