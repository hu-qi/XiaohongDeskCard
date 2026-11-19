#ifndef XIAOHONG_DESK_WS63_CMSIS_H
#define XIAOHONG_DESK_WS63_CMSIS_H

#include "cmsis_os2.h"
#include "desk_ws63_glue.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * CMSIS-RTOS2 display event adapter for the original Xiaohong LvglTask.
 *
 * No upstream enum or queue symbol is hard-coded. Pass the real
 * g_disp_event_qid and the unique eDisp_Desk_Update value from the
 * VERIFIED upstream disp_driver.h revision.
 *
 * A 4-byte queue message is mandatory. Check actual queue item size before
 * enabling any producers; using a mismatched size risks reading beyond an
 * event variable when osMessageQueuePut copies its item.
 */
typedef struct {
    osMessageQueueId_t display_events;
    uint32_t update_event;
    uint32_t failed_post_count; /* UI/task-local diagnostic; not atomic. */
    bool ready;
} desk_ws63_cmsis_t;

/* Call during UI task initialization, after osMessageQueueNew. */
bool desk_ws63_cmsis_init(desk_ws63_cmsis_t *adapter,
                          osMessageQueueId_t display_events,
                          uint32_t update_event);

/*
 * Callable from non-ISR worker context. Nonblocking (timeout = 0).
 * False means the queue may be full or unusable. The UI task MUST ALSO
 * run desk_ws63_cmsis_ui_poll periodically, so dropped wakes do not leave
 * the Desk Dispatcher permanently stuck.
 */
bool desk_ws63_cmsis_post_update(desk_ws63_cmsis_t *adapter);

/* desk_ws63_callbacks_t.post_display_event callback; no wait or socket IO. */
void desk_ws63_cmsis_notify(void *adapter_context);

/*
 * Only from the existing LvglTask: on receipt of update_event and from its
 * regular timer/poll path. The function is deliberately idempotent.
 */
unsigned desk_ws63_cmsis_ui_poll(desk_ws63_cmsis_t *adapter,
                                desk_ws63_port_t *port);

#endif
