#include "desk_wifi_ingress.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    desk_content_t display;
    desk_content_t stored;
    uint32_t saved_revision;
    unsigned saves;
    unsigned renders;
    unsigned replies;
    bool currently_authorized;
    desk_channel_t channel;
    uint32_t peer;
    uint8_t ack_bytes[DESK_ACK_SIZE];
} fixture_t;

static int persist(void *ctx, const desk_content_t *value, uint32_t revision)
{
    fixture_t *f = (fixture_t *)ctx;
    f->stored = *value;
    f->saved_revision = revision;
    f->saves++;
    return 1;
}
static int display(void *ctx, const desk_content_t *content)
{
    fixture_t *f = (fixture_t *)ctx;
    f->display = *content;
    f->renders++;
    return 1;
}
static bool verify_on_ui(void *ctx, desk_channel_t channel, uint32_t peer)
{
    fixture_t *f = (fixture_t *)ctx;
    return f->currently_authorized && channel == DESK_CHANNEL_WIFI &&
           (peer == 101u || peer == 102u);
}
static void result(void *ctx, desk_channel_t channel, uint32_t peer,
                   const uint8_t ack[DESK_ACK_SIZE])
{
    fixture_t *f = (fixture_t *)ctx;
    f->channel = channel;
    f->peer = peer;
    memcpy(f->ack_bytes, ack, DESK_ACK_SIZE);
    f->replies++;
}

static const char json1[] =
    "{\"requestId\":55,\"expectedRevision\":0,"
    "\"content\":{\"name\":\"张三\",\"title\":\"研发负责人\","
    "\"organization\":\"OpenHarmony\",\"theme\":\"classic\"}}";
static const char json2[] =
    "{\"requestId\":56,\"expectedRevision\":1,"
    "\"content\":{\"name\":\"李四\",\"title\":\"开发者\","
    "\"organization\":\"OpenHarmony\",\"theme\":\"modern\"}}";

static desk_wifi_result_t put(desk_dispatcher_t *dispatcher, uint32_t peer,
                              bool authorized, const char *payload, uint32_t *id)
{
    return desk_wifi_submit_put(dispatcher, peer, authorized,
                                (const uint8_t *)payload, strlen(payload), id);
}

int main(void)
{
    fixture_t f = {0};
    desk_core_t core = {0};
    desk_hooks_t core_hooks = {NULL, persist, display, &f};
    desk_core_init(&core, core_hooks);
    desk_dispatcher_t dispatcher = {0};
    desk_dispatch_hooks_t dispatch_hooks = {0};
    dispatch_hooks.on_result = result;
    dispatch_hooks.authorize_on_ui = verify_on_ui;
    dispatch_hooks.context = &f;
    assert(desk_dispatcher_init(&dispatcher, &core, dispatch_hooks));
    f.currently_authorized = true;

    uint32_t id = 999u;
    assert(put(&dispatcher, 101u, false, json1, &id) == DESK_WIFI_UNAUTHORIZED);
    assert(put(&dispatcher, 101u, true, "{broken", &id) == DESK_WIFI_BAD_REQUEST);
    assert(put(&dispatcher, 0u, true, json1, &id) == DESK_WIFI_BAD_REQUEST);
    assert(desk_dispatcher_pending(&dispatcher) == 0u && id == 999u);

    assert(put(&dispatcher, 101u, true, json1, &id) == DESK_WIFI_QUEUED);
    assert(id == 55u && desk_dispatcher_pending(&dispatcher) == 1u);
    assert(f.renders == 0u); /* HTTP worker must not render directly. */
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(core.revision == 1u && f.saved_revision == 1u);
    assert(f.renders == 1u && f.saves == 1u && f.replies == 1u);
    assert(f.channel == DESK_CHANNEL_WIFI && f.peer == 101u);
    desk_ack_t ack = {0};
    assert(!desk_wifi_match_ack(56u, f.ack_bytes, DESK_ACK_SIZE, &ack));
    assert(desk_wifi_match_ack(55u, f.ack_bytes, DESK_ACK_SIZE, &ack));
    assert(ack.result == DESK_OK && ack.revision == 1u);

    /* GET response serializes a snapshot taken on the UI owner task. */
    desk_http_state_t state = {0};
    state.revision = core.revision;
    state.content = core.content;
    state.display = f.display;
    char status[DESK_HTTP_MAX_RESPONSE];
    assert(desk_http_encode_status(&state, status, sizeof(status)) != 0u);
    assert(strstr(status, "\"revision\":1") != NULL);
    assert(strstr(status, "张三") != NULL);
    assert(strstr(status, "\"hardwareValidated\":false") != NULL);

    /* Device owner revoked AFTER HTTP submit, BEFORE UI execution. */
    assert(put(&dispatcher, 102u, true, json2, &id) == DESK_WIFI_QUEUED);
    f.currently_authorized = false;
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(desk_wifi_match_ack(56u, f.ack_bytes, DESK_ACK_SIZE, &ack));
    assert(ack.result == DESK_UNAUTHORIZED);
    assert(core.revision == 1u && f.saves == 1u && f.renders == 1u);
    f.currently_authorized = true;

    /* Same command can be explicitly submitted after owner is restored. */
    assert(put(&dispatcher, 102u, true, json2, &id) == DESK_WIFI_QUEUED);
    assert(desk_dispatcher_process_one(&dispatcher));
    assert(core.revision == 2u && f.saves == 2u && f.renders == 2u);
    assert(strcmp(f.display.name, "李四") == 0);
    assert(desk_wifi_match_ack(56u, f.ack_bytes, DESK_ACK_SIZE, &ack));
    assert(ack.result == DESK_OK && ack.revision == 2u);

    char response[DESK_HTTP_MAX_RESPONSE];
    state.revision = core.revision;
    state.content = core.content;
    state.display = f.display;
    assert(desk_http_encode_reply(ack.result, &state, response,
                                  sizeof(response)) > 0u);
    assert(strstr(response, "\"result\":\"OK\"") != NULL);
    assert(strstr(response, "\"transport\":\"WIFI_HTTP\"") != NULL);

    uint8_t corrupted[DESK_ACK_SIZE];
    memcpy(corrupted, f.ack_bytes, sizeof(corrupted));
    corrupted[11] ^= 1u;
    assert(!desk_wifi_match_ack(56u, corrupted, sizeof(corrupted), &ack));

    /* Backpressure on the SAME dispatcher is shared by Wi-Fi/BLE/SLE. */
    for (unsigned i = 0; i < DESK_DISPATCH_QUEUE_CAPACITY; ++i) {
        desk_dispatch_item_t item = {0};
        item.channel = i % 2u == 0u ? DESK_CHANNEL_BLE : DESK_CHANNEL_SLE;
        item.peer_token = 1000u + i;
        item.command.request_id = 100u + i;
        item.authorized = false;
        assert(desk_dispatcher_submit(&dispatcher, &item) == DESK_DISPATCH_QUEUED);
    }
    id = 999u;
    assert(put(&dispatcher, 101u, true, json2, &id) == DESK_WIFI_BUSY);
    assert(id == 999u && desk_dispatcher_pending(&dispatcher) == 4u);
    while (desk_dispatcher_process_one(&dispatcher)) {}
    assert(core.revision == 2u);

    puts("PASS: Wi-Fi JSON->shared FIFO->UI authorization->ACK/status/backpressure");
    return 0;
}
