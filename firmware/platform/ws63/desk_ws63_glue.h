#ifndef XIAOHONG_DESK_WS63_GLUE_H
#define XIAOHONG_DESK_WS63_GLUE_H

#include "desk_dispatcher.h"
#include "desk_view_lvgl.h"

/*
 * SDK-independent glue for insertion into Xiaohong's LvglTask.
 * This is NOT a complete WS63/OpenHarmony driver. The platform supplies the
 * actual CMSIS mutex, display event message, NV/Flash and radio ACK worker.
 */
typedef struct {
    /* Mandatory: implement power-loss-safe, versioned, CRC-checked storage. */
    int (*load)(void *context, desk_content_t *content, uint32_t *revision);
    int (*save)(void *context, const desk_content_t *content, uint32_t revision);
    /* Mandatory: nonblocking post to existing g_disp_event_qid display queue. */
    void (*post_display_event)(void *context);
    /* Mandatory: enqueue binary business ACK for BLE/SLE/HTTP transport worker. */
    void (*enqueue_ack)(void *context, desk_channel_t channel, uint32_t peer_token,
                        const uint8_t ack[DESK_ACK_SIZE]);
    /* Optional in single-thread host tests, MANDATORY in actual multi-task WS63. */
    void (*lock)(void *context);
    void (*unlock)(void *context);
    void *context;
    /* MANDATORY: check peer connection generation & owner authorization at
     * execution time, including for Wi-Fi sessions. */
    bool (*authorize_on_ui)(void *context, desk_channel_t channel,
                            uint32_t peer_token);
} desk_ws63_callbacks_t;

typedef struct {
    desk_core_t core;
    desk_dispatcher_t dispatcher;
    desk_lvgl_view_t view;
    desk_ws63_callbacks_t callbacks;
    bool ready;
} desk_ws63_port_t;

/* Must run ONCE from the LVGL UI task after LVGL and NV are ready. */
int desk_ws63_port_init(desk_ws63_port_t *port, desk_ws63_callbacks_t callbacks,
                        lv_obj_t *parent);

/* Only call on BLE/SLE/HTTP TASKS, never ISR. Input must already be decoded. */
desk_dispatch_status_t desk_ws63_port_submit(desk_ws63_port_t *port,
                                              const desk_dispatch_item_t *item);

/* Call ONLY from the LVGL task's eDisp_Desk_Update case. */
unsigned desk_ws63_port_ui_drain(desk_ws63_port_t *port);

/* Only from LVGL task, after radio producers and ACK workers are stopped. */
void desk_ws63_port_destroy(desk_ws63_port_t *port);

#endif
