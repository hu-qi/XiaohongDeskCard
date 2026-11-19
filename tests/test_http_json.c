#include "desk_http_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *json, desk_set_command_t *command)
{
    return desk_http_decode_set((const uint8_t *)json, strlen(json), command);
}

static void rejects(const char *text)
{
    desk_set_command_t command = {.request_id = UINT32_C(0xAABBCCDD)};
    assert(!parse(text, &command));
    assert(command.request_id == UINT32_C(0xAABBCCDD));
}

int main(void)
{
    const char *plain =
        "{\"requestId\":123,\"expectedRevision\":0,"
        "\"content\":{\"name\":\"张三\",\"title\":\"研发负责人\","
        "\"organization\":\"OpenHarmony\",\"theme\":\"classic\"}}";
    desk_set_command_t cmd = {0};
    assert(parse(plain, &cmd));
    assert(cmd.request_id == 123u && cmd.expected_revision == 0u);
    assert(strcmp(cmd.content.name, "张三") == 0);
    assert(strcmp(cmd.content.title, "研发负责人") == 0);

    /* All object key orders are legal; JSON \uXXXX pairs decode as UTF-8. */
    const char *esc =
        " { \"content\":{\"theme\":\"modern\","
        "\"organization\":\"OpenHarmony\",\"title\":\"\","
        "\"name\":\"A\\uD83D\\uDE80\\u4E2D\\u6587\"},"
        "\"expectedRevision\":4294967295,\"requestId\":4294967295} ";
    assert(parse(esc, &cmd));
    assert(cmd.request_id == UINT32_MAX);
    assert(cmd.expected_revision == UINT32_MAX);
    assert(strcmp(cmd.content.name, "A🚀中文") == 0);
    assert(cmd.content.title[0] == '\0');

    const char *special =
        "{\"requestId\":2,\"expectedRevision\":1,"
        "\"content\":{\"name\":\"\\\"A\\\"\\\\B\","
        "\"title\":\"/\",\"organization\":\"x\",\"theme\":\"minimal\"}}";
    assert(parse(special, &cmd));
    assert(strcmp(cmd.content.name, "\"A\"\\B") == 0);

    rejects("{}");
    rejects("{\"requestId\":1}");
    rejects("{\"requestId\":0,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":null}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":false}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":[]}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"n\",\"title\":\"t\",\"organization\":\"o\","
            "\"theme\":\"classic\",\"unknown\":\"x\"}}");
    rejects("{\"requestId\":1,\"requestId\":2,\"expectedRevision\":0,"
            "\"content\":{\"name\":\"n\",\"title\":\"t\","
            "\"organization\":\"o\",\"theme\":\"classic\"}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,"
            "\"content\":{\"name\":\"n\",\"name\":\"x\","
            "\"title\":\"t\",\"organization\":\"o\",\"theme\":\"classic\"}}");
    rejects("{\"requestId\":01,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":-1,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":4294967296,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":1.5,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":1e0,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":true,\"expectedRevision\":0,\"content\":{}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"\\uD800\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"classic\"}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"\\uDC00\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"classic\"}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"\\u0000\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"classic\"}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"n\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"unknown\"}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"n\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"classic\"}}junk");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"n\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"classic\",}}");
    rejects("{\"requestId\":1,\"expectedRevision\":0,\"content\":{"
            "\"name\":\"n\",\"title\":\"\","
            "\"organization\":\"\",\"theme\":\"classic\"},}");

    /* Invalid UTF-8 and embedded raw control byte cannot slip through. */
    char broken[384];
    strcpy(broken, plain);
    char *name = strstr(broken, "张三");
    assert(name != NULL);
    name[0] = (char)0xC0;
    rejects(broken);
    assert(!desk_http_decode_set((const uint8_t *)plain, 0u, &cmd));
    assert(!desk_http_decode_set((const uint8_t *)plain, DESK_HTTP_MAX_BODY + 1u, &cmd));

    /* Valid snapshots serialize actual configured and displayed values.
     * JSON escaping is important for quotes and backslash in names. */
    desk_http_state_t state = {0};
    state.revision = 21u;
    strcpy(state.content.name, "张三\"A");
    strcpy(state.content.title, "CTO");
    strcpy(state.content.organization, "OpenHarmony");
    strcpy(state.content.theme, "modern");
    state.display = state.content;
    state.hardware_validated = false;
    char buffer[DESK_HTTP_MAX_RESPONSE];
    size_t n = desk_http_encode_status(&state, buffer, sizeof(buffer));
    assert(n > 0u && n == strlen(buffer));
    assert(strstr(buffer, "\"revision\":21") != NULL);
    assert(strstr(buffer, "张三\\\"A") != NULL);
    assert(strstr(buffer, "\"hardwareValidated\":false") != NULL);
    assert(strstr(buffer, "\"display\":{") != NULL);
    assert(desk_http_encode_status(&state, buffer, 8u) == 0u);

    n = desk_http_encode_reply(DESK_CONFLICT, &state, buffer, sizeof(buffer));
    assert(n > 0u && n == strlen(buffer));
    assert(strstr(buffer, "\"result\":\"CONFLICT\"") != NULL);
    assert(strstr(buffer, "\"transport\":\"WIFI_HTTP\"") != NULL);
    assert(desk_http_encode_reply(DESK_OK, &state, buffer, 3u) == 0u);
    strcpy(state.content.theme, "unlisted");
    assert(desk_http_encode_reply(DESK_OK, &state, buffer, sizeof(buffer)) == 0u);

    puts("PASS: strict Wi-Fi JSON parse Unicode/bounds/duplicates; GET/PUT response codec");
    return 0;
}
