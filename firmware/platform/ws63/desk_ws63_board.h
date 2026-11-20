#ifndef XIAOHONG_DESK_WS63_BOARD_H
#define XIAOHONG_DESK_WS63_BOARD_H

#include "desk_nv_store.h"
#include "desk_ws63_transport.h"

/*
 * Board binding that connects the portable Desk Core to the original Xiaohong
 * AI WS63 firmware's own LVGL task. Call desk_board_init() once from LvglTask
 * after lv_init()/disp_create_ui(), and desk_board_poll() from the same task's
 * main loop.
 *
 * Transport entry points (BLE/SLE/HTTP) are owned by desk_board_transport();
 * the vendor service code calls into them and drains the reply mailbox.
 */
int desk_board_init(void);
void desk_board_poll(void);
int desk_board_is_ready(void);

/* The transport hub (BLE/SLE/HTTP ingress + reply mailbox), or NULL. */
desk_transport_t *desk_board_transport(void);

#if defined(DESK_BOARD_HOST_TEST)
/* Host-only: inject the NV HAL that the WS63 build takes from uapi_nv_*. */
void desk_board_test_set_nv_io(desk_nv_io_t io);
#endif

#endif
