/*
 * Station-mode HTTP listener for the Desk Card API.
 *
 * The vendor's AP provisioning server (net_http_server.c) is not compiled into
 * this target, and it is bound to the AP interface anyway. The Desk Card needs
 * a listener on the station interface so the Harmony app can reach
 * /api/v1/desk/status and /api/v1/desk/content over the same Wi-Fi the device
 * already joined.
 *
 * This file is SDK-only; it has no host build. All protocol handling lives in
 * desk_ws63_adapters.c, which is host-tested.
 */
#include "desk_ws63_http.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cmsis_os2.h"
#include "errcode.h"
#include "lwip/api.h"
#include "lwip/netifapi.h"
#include "osal_debug.h"
#include "osal_task.h"

#include "desk_http_json.h"
#include "desk_ws63_adapters.h"

#define DESK_HTTP_PORT 80
#define DESK_HTTP_STACK (1024u * 8u)
#define DESK_HTTP_PRIO 24u
#define DESK_HTTP_RECV_TIMEOUT_MS 3000
#define DESK_HTTP_REQ_MAX 2048u
#define DESK_HTTP_POLL_MS 200u

/* The station interface the device joined. */
static const char DESK_HTTP_IFNAME[] = "wlan0";

static volatile bool g_running;
static volatile bool g_stop;
static osThreadId_t g_thread;

static err_t write_all(struct netconn *conn, const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t sent = 0u;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > 0xFFFFu) chunk = 0xFFFFu;
        const err_t err = netconn_write(conn, p + sent, (uint16_t)chunk,
                                        NETCONN_COPY);
        if (err != ERR_OK) return err;
        sent += chunk;
    }
    return ERR_OK;
}

static const char *status_line(desk_http_result_t result)
{
    switch (result) {
        case DESK_HTTP_HANDLED_UNAUTH: return "HTTP/1.1 401 Unauthorized\r\n";
        case DESK_HTTP_HANDLED_BAD: return "HTTP/1.1 400 Bad Request\r\n";
        case DESK_HTTP_HANDLED_BUSY:
            return "HTTP/1.1 429 Too Many Requests\r\n";
        case DESK_HTTP_HANDLED_OK:
        default: return "HTTP/1.1 200 OK\r\n";
    }
}

/* Case-insensitive search for "Content-Length:" in the header block. */
static bool content_length(const char *headers, size_t *out)
{
    const char *p = headers;
    while (*p != '\0') {
        size_t i = 0;
        static const char name[] = "content-length:";
        while (name[i] != '\0' && p[i] != '\0') {
            char a = p[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != name[i]) break;
            i++;
        }
        if (name[i] == '\0') {
            const char *v = p + i;
            while (*v == ' ' || *v == '\t') v++;
            size_t n = 0u;
            while (*v >= '0' && *v <= '9') {
                n = n * 10u + (size_t)(*v - '0');
                v++;
            }
            *out = n;
            return true;
        }
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return false;
}

/* Append up to `want` more body bytes from the connection. */
static size_t read_more_body(struct netconn *conn, uint8_t *dst, size_t cap)
{
    size_t got = 0u;
    while (got < cap) {
        struct netbuf *more = NULL;
        if (netconn_recv(conn, &more) != ERR_OK || more == NULL) break;
        char *chunk = NULL;
        u16_t chunk_len = 0;
        netbuf_data(more, (void **)&chunk, &chunk_len);
        size_t copy = chunk_len;
        if (copy > cap - got) copy = cap - got;
        if (chunk != NULL && copy > 0u) {
            memcpy(dst + got, chunk, copy);
            got += copy;
        }
        netbuf_delete(more);
    }
    return got;
}

static void serve_one(struct netconn *conn)
{
    struct netbuf *inbuf = NULL;
    if (netconn_recv(conn, &inbuf) != ERR_OK || inbuf == NULL) return;

    char *buf = NULL;
    u16_t buflen = 0;
    netbuf_data(inbuf, (void **)&buf, &buflen);
    if (buf == NULL || buflen == 0u) {
        netbuf_delete(inbuf);
        return;
    }

    /* NUL-terminated copy: the adapters use C string helpers on the request
     * line and the header block. Single task, so a static buffer is safe and
     * keeps 2 KB off the thread stack. */
    static char req[DESK_HTTP_REQ_MAX];
    size_t copy = buflen < sizeof(req) - 1u ? buflen : sizeof(req) - 1u;
    memcpy(req, buf, copy);
    req[copy] = '\0';
    netbuf_delete(inbuf);

    char *line_end = strstr(req, "\r\n");
    if (line_end == NULL) return;
    const size_t line_len = (size_t)(line_end - req);
    char request_line[128];
    if (line_len >= sizeof(request_line)) return;
    memcpy(request_line, req, line_len);
    request_line[line_len] = '\0';

    /* Terminate the header block at the blank line so the body is never
     * scanned as a header. */
    char *body_sep = strstr(req, "\r\n\r\n");
    uint8_t *body = NULL;
    size_t body_len = 0u;
    if (body_sep != NULL) {
        *body_sep = '\0';
        body = (uint8_t *)(body_sep + 4);
        body_len = copy - (size_t)(body - (uint8_t *)req);

        /* The body may have arrived in a later segment than the headers. */
        size_t want = 0u;
        if (content_length(req, &want) && want > body_len) {
            size_t more = want - body_len;
            const size_t room = sizeof(req) - 1u - copy;
            if (more > room) more = room;
            body_len += read_more_body(conn, body + body_len, more);
        }
    }

    char out[DESK_HTTP_MAX_RESPONSE];
    size_t out_len = 0u;
    const desk_http_result_t result = desk_http_handle_request(
        request_line, req, body, body_len, out, sizeof(out), &out_len);

    if (result == DESK_HTTP_NOT_HANDLED) {
        static const char not_found[] =
            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        (void)write_all(conn, not_found, sizeof(not_found) - 1u);
        return;
    }

    char header[160];
    const int n = snprintf(header, sizeof(header),
                           "%sContent-Type: application/json; charset=UTF-8\r\n"
                           "Content-Length: %u\r\nConnection: close\r\n\r\n",
                           status_line(result), (unsigned int)out_len);
    if (n <= 0 || (size_t)n >= sizeof(header)) return;
    (void)write_all(conn, header, (size_t)n);
    if (out_len > 0u) (void)write_all(conn, out, out_len);
}

/*
 * Wait until the station interface has a usable IPv4 address. The listener is
 * started from the UI task at boot, long before Wi-Fi associates, so blocking
 * here keeps the rest of the firmware free of Wi-Fi ordering assumptions and
 * makes a reconnect after a dropped link work without any extra hook.
 */
static bool wait_for_station_ip(ip_addr_t *out)
{
    uint32_t waited_ms = 0u;
    while (!g_stop) {
        struct netif *netif_p = netifapi_netif_find(DESK_HTTP_IFNAME);
        if (netif_p != NULL && ip_addr_get_ip4_u32(&netif_p->ip_addr) != 0u) {
            *out = netif_p->ip_addr;
            return true;
        }
        osal_msleep(DESK_HTTP_POLL_MS);
        waited_ms += DESK_HTTP_POLL_MS;
        if (waited_ms % 10000u == 0u) {
            osal_printk("[desk http] waiting for %s ip (%u ms)\r\n",
                        DESK_HTTP_IFNAME, (unsigned int)waited_ms);
        }
    }
    return false;
}

static void desk_http_task(void *arg)
{
    (void)arg;
    g_running = true;

    while (!g_stop) {
        ip_addr_t bind_ip;
        if (!wait_for_station_ip(&bind_ip)) break;

        struct netconn *listener = netconn_new(NETCONN_TCP);
        if (listener == NULL) {
            osal_msleep(DESK_HTTP_POLL_MS);
            continue;
        }
        if (netconn_bind(listener, &bind_ip, DESK_HTTP_PORT) != ERR_OK ||
            netconn_listen(listener) != ERR_OK) {
            osal_printk("[desk http] bind/listen on port %d failed\r\n",
                        DESK_HTTP_PORT);
            netconn_close(listener);
            netconn_delete(listener);
            osal_msleep(DESK_HTTP_POLL_MS);
            continue;
        }

        osal_printk("[desk http] listening on %s:%d\r\n", DESK_HTTP_IFNAME,
                    DESK_HTTP_PORT);

        while (!g_stop) {
            struct netconn *client = NULL;
            const err_t err = netconn_accept(listener, &client);
            if (err != ERR_OK || client == NULL) {
                /* A dropped link invalidates the bind address; go back and wait
                 * for the station to come up again. */
                struct netif *netif_p = netifapi_netif_find(DESK_HTTP_IFNAME);
                if (netif_p == NULL ||
                    ip_addr_get_ip4_u32(&netif_p->ip_addr) == 0u) break;
                osal_msleep(DESK_HTTP_POLL_MS);
                continue;
            }
            (void)netconn_set_recvtimeout(client, DESK_HTTP_RECV_TIMEOUT_MS);
            serve_one(client);
            netconn_close(client);
            netconn_delete(client);
        }

        netconn_close(listener);
        netconn_delete(listener);
    }

    g_running = false;
    g_thread = NULL;
    osal_printk("[desk http] stopped\r\n");
    osThreadExit();
}

int desk_http_task_start(void)
{
    if (g_running) return 0;
    g_stop = false;
    const osThreadAttr_t attr = {
        .name = "DeskHttp",
        .stack_size = DESK_HTTP_STACK,
        .priority = (osPriority_t)DESK_HTTP_PRIO,
    };
    g_thread = osThreadNew((osThreadFunc_t)desk_http_task, NULL, &attr);
    return g_thread != NULL ? 1 : 0;
}

void desk_http_task_stop(void)
{
    if (!g_running) return;
    g_stop = true;
}
