#include "desk_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static size_t bounded_len(const char *s, size_t capacity)
{
    size_t n = 0;
    if (s == NULL) return capacity;
    while (n < capacity && s[n] != '\0') n++;
    return n;
}

/* Reject invalid UTF-8, overlong sequences, surrogates, controls and noncharacters. */
static bool clean_utf8(const char *text, size_t capacity, bool mandatory)
{
    const size_t n = bounded_len(text, capacity);
    size_t i = 0;
    if (n == capacity || (mandatory && n == 0)) return false;
    while (i < n) {
        unsigned char a = (unsigned char)text[i];
        uint32_t cp;
        size_t count;
        if (a < 0x80u) { cp = a; count = 1; }
        else if (a >= 0xC2u && a <= 0xDFu) { cp = a & 0x1Fu; count = 2; }
        else if (a >= 0xE0u && a <= 0xEFu) { cp = a & 0x0Fu; count = 3; }
        else if (a >= 0xF0u && a <= 0xF4u) { cp = a & 0x07u; count = 4; }
        else return false;
        if (i + count > n) return false;
        for (size_t j = 1; j < count; ++j) {
            unsigned char b = (unsigned char)text[i+j];
            if ((b & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if ((count == 2 && cp < 0x80u) ||
            (count == 3 && cp < 0x800u) ||
            (count == 4 && cp < 0x10000u) ||
            cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu) ||
            (cp >= 0xFDD0u && cp <= 0xFDEFu) ||
            (cp & 0xFFFEu) == 0xFFFEu || cp < 0x20u ||
            (cp >= 0x7Fu && cp <= 0x9Fu)) return false;
        i += count;
    }
    return true;
}

bool desk_content_valid(const desk_content_t *content)
{
    if (content == NULL) return false;
    return clean_utf8(content->name, sizeof(content->name), true) &&
           clean_utf8(content->title, sizeof(content->title), false) &&
           clean_utf8(content->organization, sizeof(content->organization), false) &&
           clean_utf8(content->theme, sizeof(content->theme), true) &&
           (strcmp(content->theme, "classic") == 0 ||
            strcmp(content->theme, "modern") == 0 ||
            strcmp(content->theme, "minimal") == 0);
}

static bool same_content(const desk_content_t *a, const desk_content_t *b)
{
    return strcmp(a->name, b->name) == 0 && strcmp(a->title, b->title) == 0 &&
           strcmp(a->organization, b->organization) == 0 &&
           strcmp(a->theme, b->theme) == 0;
}

void desk_core_init(desk_core_t *core, desk_hooks_t hooks)
{
    if (core == NULL) return;
    memset(core, 0, sizeof(*core));
    core->hooks = hooks;
    (void)strcpy(core->content.name, "Xiaohong");
    (void)strcpy(core->content.title, "Desk Card");
    /* The organization renders in the coloured header band. An empty string
     * leaves the band a solid colour block with no text at all, which reads
     * as a rendering fault on a fresh device. Ship a Chinese default so the
     * band is legible before the first SET. */
    (void)strcpy(core->content.organization, "小鸿智能桌牌");
    (void)strcpy(core->content.theme, "classic");

    if (hooks.load != NULL) {
        desk_content_t loaded = {0};
        uint32_t rev = 0;
        if (hooks.load(hooks.user, &loaded, &rev) && desk_content_valid(&loaded)) {
            core->content = loaded;
            core->revision = rev;
        }
    }
}

desk_result_t desk_core_apply(desk_core_t *core, const desk_set_command_t *command,
                               bool authorized)
{
    if (core == NULL || command == NULL || !desk_content_valid(&command->content) ||
        command->request_id == 0u) return DESK_BAD_INPUT;
    if (!authorized) return DESK_UNAUTHORIZED;
    if (core->has_last_request && core->last_request_id == command->request_id) {
        if (core->last_request_revision == core->revision &&
            core->last_expected_revision == command->expected_revision &&
            same_content(&core->content, &command->content)) {
            /* A prior render may have failed after persistence: repaint on retry. */
            return desk_core_repaint(core);
        }
        return DESK_DUPLICATE_ID;
    }
    if (command->expected_revision != core->revision) return DESK_CONFLICT;
    if (core->revision == UINT32_MAX) return DESK_REVISION_EXHAUSTED;

    const uint32_t revision = core->revision + 1u;
    /* Storage precedes rendering; a render failure is recoverable by repaint. */
    if (core->hooks.save != NULL &&
        !core->hooks.save(core->hooks.user, &command->content, revision)) {
        return DESK_STORE_ERROR;
    }
    core->content = command->content;
    core->revision = revision;
    core->last_request_id = command->request_id;
    core->last_request_revision = revision;
    core->last_expected_revision = command->expected_revision;
    core->has_last_request = true;
    return desk_core_repaint(core);
}

desk_result_t desk_core_repaint(desk_core_t *core)
{
    if (core == NULL) return DESK_BAD_INPUT;
    if (core->hooks.render != NULL &&
        !core->hooks.render(core->hooks.user, &core->content)) return DESK_RENDER_ERROR;
    return DESK_OK;
}

const char *desk_result_name(desk_result_t result)
{
    switch (result) {
        case DESK_OK: return "OK";
        case DESK_BAD_INPUT: return "BAD_INPUT";
        case DESK_UNAUTHORIZED: return "UNAUTHORIZED";
        case DESK_CONFLICT: return "CONFLICT";
        case DESK_DUPLICATE_ID: return "DUPLICATE_ID";
        case DESK_STORE_ERROR: return "STORE_ERROR";
        case DESK_RENDER_ERROR: return "RENDER_ERROR";
        case DESK_REVISION_EXHAUSTED: return "REVISION_EXHAUSTED";
        default: return "UNKNOWN";
    }
}
