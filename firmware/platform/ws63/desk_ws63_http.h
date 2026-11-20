#ifndef XIAOHONG_DESK_WS63_HTTP_H
#define XIAOHONG_DESK_WS63_HTTP_H

/*
 * Station-mode HTTP listener for the Desk Card API.
 *
 * The vendor's own net_http_server.c is the AP provisioning page and is not
 * compiled into this target. This worker owns a small lwIP netconn listener on
 * the station interface and hands each request to desk_http_handle_request();
 * everything it does not claim is answered 404, so it never serves the
 * provisioning page by accident.
 *
 * Start it once after the station has an IP (see desk_http_task_start()).
 */

/* Start the listener task. Returns 1 when the task was created, 0 on failure
 * or when it is already running. Safe to call again after a disconnect. */
int desk_http_task_start(void);

/* Stop listening and release the socket. The task exits on its own. */
void desk_http_task_stop(void);

#endif
