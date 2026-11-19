#include "desk_ws63_cmsis.h"

#include <string.h>

bool desk_ws63_cmsis_init(desk_ws63_cmsis_t *adapter,
                          osMessageQueueId_t display_events,
                          uint32_t update_event)
{
    if (adapter == NULL || display_events == NULL ||
        osMessageQueueGetMsgSize(display_events) != sizeof(uint32_t))
        return false;

    memset(adapter, 0, sizeof(*adapter));
    adapter->display_events = display_events;
    adapter->update_event = update_event;
    adapter->ready = true;
    return true;
}

bool desk_ws63_cmsis_post_update(desk_ws63_cmsis_t *adapter)
{
    if (adapter == NULL || !adapter->ready) return false;
    const uint32_t event = adapter->update_event;
    if (osMessageQueuePut(adapter->display_events, &event, 0u, 0u) != osOK) {
        if (adapter->failed_post_count < UINT32_MAX)
            adapter->failed_post_count++;
        return false;
    }
    return true;
}

void desk_ws63_cmsis_notify(void *adapter_context)
{
    (void)desk_ws63_cmsis_post_update(
        (desk_ws63_cmsis_t *)adapter_context);
}

unsigned desk_ws63_cmsis_ui_poll(desk_ws63_cmsis_t *adapter,
                                desk_ws63_port_t *port)
{
    if (adapter == NULL || !adapter->ready || port == NULL)
        return 0u;
    return desk_ws63_port_ui_drain(port);
}
