#ifndef XIAOHONG_DESK_WS63_BOARD_H
#define XIAOHONG_DESK_WS63_BOARD_H

/*
 * Minimal board binding that connects the portable Desk Core to the original
 * Xiaohong AI WS63 firmware's own LVGL task. Call desk_board_init() once from
 * LvglTask after lv_init()/disp_create_ui(), and desk_board_poll() from the
 * same task's main loop (or on the desk display event).
 *
 * Only NV persistence and display are wired. BLE/SLE/Wi-Fi transports are not
 * registered by this build, so desk commands from radios are not accepted yet.
 */
int desk_board_init(void);
void desk_board_poll(void);
int desk_board_is_ready(void);

#if defined(DESK_BOARD_HOST_TEST)
#include "desk_nv_store.h"
/* Host-only: inject the NV HAL that the WS63 build takes from uapi_nv_*. */
void desk_board_test_set_nv_io(desk_nv_io_t io);
#endif

#endif
