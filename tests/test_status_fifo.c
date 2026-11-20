#include "desk_wifi_ingress.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    desk_content_t displayed;
    desk_content_t stored;
    uint32_t stored_revision;
    bool authorized;
    bool fail_render;
    unsigned saves;
    unsigned renders;
    unsigned writes;
    unsigned reads;
    char order[32];
    unsigned order_len;
    struct {
        uint32_t peer;
        desk_result_t result;
        desk_http_state_t state;
        bool has_state;
    } status[8];
    desk_ack_t ack[8];
} fixture_t;

static int save(void *ctx, const desk_content_t *value, uint32_t revision)
{
    fixture_t *f = (fixture_t *)ctx;
    f->stored = *value;
    f->stored_revision = revision;
    f->saves++;
    return 1;
}
static int render(void *ctx, const desk_content_t *value)
{
    fixture_t *f = (fixture_t *)ctx;
    if (f->fail_render) return 0;
    f->displayed = *value;
    f->renders++;
    return 1;
}
static bool authorize(void *ctx, desk_channel_t channel, uint32_t peer)
{
    fixture_t *f = (fixture_t *)ctx;
    return f->authorized && channel == DESK_CHANNEL_WIFI && peer > 0u;
}
static bool get_display(void *ctx, desk_content_t *out)
{
    fixture_t *f = (fixture_t *)ctx;
    *out = f->displayed;
    return true;
}
static void on_write(void *ctx, desk_channel_t channel, uint32_t peer,
                     const uint8_t ack[DESK_ACK_SIZE])
{
    fixture_t *f = (fixture_t *)ctx;
    assert(channel == DESK_CHANNEL_WIFI && peer != 0u);
    assert(f->writes < 8u && f->order_len < sizeof(f->order));
    assert(desk_wire_decode_ack(ack, DESK_ACK_SIZE,
                                &f->ack[f->writes]) == DESK_WIRE_OK);
    f->writes++;
    f->order[f->order_len++] = 'W';
}
static void on_read(void *ctx, desk_channel_t channel, uint32_t peer,
                    desk_result_t result, const desk_http_state_t *snapshot)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(channel == DESK_CHANNEL_WIFI && peer != 0u);
    assert(f->reads < 8u && f->order_len < sizeof(f->order));
    const unsigned i = f->reads++;
    f->status[i].peer = peer;
    f->status[i].result = result;
    f->status[i].has_state = snapshot != NULL;
    if (snapshot != NULL) f->status[i].state = *snapshot;
    f->order[f->order_len++] = 'R';
}

static const char command1[] =
    "{\"requestId\":20,\"expectedRevision\":0,"
    "\"content\":{\"name\":\"张三\",\"title\":\"负责人\","
    "\"organization\":\"OpenHarmony\",\"theme\":\"classic\"}}";
static const char command2[] =
    "{\"requestId\":21,\"expectedRevision\":1,"
    "\"content\":{\"name\":\"李四\",\"title\":\"负责人\","
    "\"organization\":\"OpenHarmony\",\"theme\":\"modern\"}}";

static desk_wifi_result_t put(desk_dispatcher_t *dispatcher, uint32_t peer,
                              const char *json)
{
    uint32_t id = 0u;
    return desk_wifi_submit_put(dispatcher, peer, true,
                                (const uint8_t *)json, strlen(json), &id);
}

int main(void)
{
    fixture_t f = {0};
    f.authorized = true;
    desk_core_t core;
    const desk_hooks_t core_hooks = {NULL, save, render, &f};
    desk_core_init(&core, core_hooks);
    assert(desk_core_repaint(&core) == DESK_OK);

    desk_dispatcher_t d = {0};
    desk_dispatch_hooks_t hooks = {0};
    hooks.context = &f;
    hooks.authorize_on_ui = authorize;
    hooks.on_result = on_write;
    hooks.on_status = on_read;
    hooks.read_display_on_ui = get_display;
    assert(desk_dispatcher_init(&d, &core, hooks));

    /* Read -> write -> read, all queued before UI starts. Sequential
     * processing MUST produce revision 0, ACK revision 1, revision 1. */
    assert(desk_wifi_submit_get(&d, 11u, true) == DESK_WIFI_QUEUED);
    assert(put(&d, 12u, command1) == DESK_WIFI_QUEUED);
    assert(desk_wifi_submit_get(&d, 13u, true) == DESK_WIFI_QUEUED);
    assert(f.reads == 0u && f.writes == 0u);
    assert(desk_dispatcher_process_one(&d));
    assert(desk_dispatcher_process_one(&d));
    assert(desk_dispatcher_process_one(&d));
    assert(f.order_len == 3u && memcmp(f.order, "RWR", 3u) == 0);
    assert(f.status[0].peer == 11u && f.status[0].result == DESK_OK);
    assert(f.status[0].state.revision == 0u);
    assert(strcmp(f.status[0].state.display.name, "Xiaohong") == 0);
    assert(f.ack[0].result == DESK_OK && f.ack[0].revision == 1u);
    assert(f.status[1].peer == 13u && f.status[1].state.revision == 1u);
    assert(strcmp(f.status[1].state.display.name, "张三") == 0);
    assert(f.saves == 1u && f.renders == 2u);

    /* Cancel ownership while a GET is already queued. No data may leak. */
    assert(desk_wifi_submit_get(&d, 14u, true) == DESK_WIFI_QUEUED);
    f.authorized = false;
    assert(desk_dispatcher_process_one(&d));
    assert(f.status[2].result == DESK_UNAUTHORIZED);
    assert(!f.status[2].has_state);
    f.authorized = true;

    /* A completed save whose display render failed must be visible as
     * diverged content/display in a subsequent consistent status snapshot. */
    f.fail_render = true;
    assert(put(&d, 15u, command2) == DESK_WIFI_QUEUED);
    assert(desk_dispatcher_process_one(&d));
    assert(f.ack[1].result == DESK_RENDER_ERROR);
    assert(f.ack[1].revision == 2u);
    assert(desk_wifi_submit_get(&d, 16u, true) == DESK_WIFI_QUEUED);
    assert(desk_dispatcher_process_one(&d));
    assert(f.status[3].result == DESK_OK && f.status[3].has_state);
    assert(f.status[3].state.revision == 2u);
    assert(strcmp(f.status[3].state.content.name, "李四") == 0);
    assert(strcmp(f.status[3].state.display.name, "张三") == 0);
    f.fail_render = false;

    /* GET uses same queue slots as all three radio writes: no starvation
     * through an unbounded status queue, and rejection never reads core. */
    for (unsigned i = 0u; i < DESK_DISPATCH_QUEUE_CAPACITY; ++i)
        assert(desk_wifi_submit_get(&d, i + 20u, true) == DESK_WIFI_QUEUED);
    assert(desk_wifi_submit_get(&d, 50u, true) == DESK_WIFI_BUSY);
    assert(put(&d, 51u, command1) == DESK_WIFI_BUSY);
    assert(desk_dispatcher_pending(&d) == DESK_DISPATCH_QUEUE_CAPACITY);
    while (desk_dispatcher_process_one(&d)) {}
    assert(f.reads == 8u);
    assert(core.revision == 2u);

    /* A missing status response sink must be rejected, not silently queued. */
    desk_dispatcher_t broken;
    desk_dispatch_hooks_t absent = hooks;
    absent.on_status = NULL;
    assert(desk_dispatcher_init(&broken, &core, absent));
    assert(desk_dispatcher_submit_status(&broken, DESK_CHANNEL_WIFI, 70u, true)
           == DESK_DISPATCH_INVALID);

    puts("PASS: ordered GET/PUT, revocation privacy, render divergence and backpressure");
    return 0;
}
