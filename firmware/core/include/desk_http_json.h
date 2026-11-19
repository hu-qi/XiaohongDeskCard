#ifndef XIAOHONG_DESK_HTTP_JSON_H
#define XIAOHONG_DESK_HTTP_JSON_H

#include "desk_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Portable, allocation-free JSON body codec for the Wi-Fi Desk HTTP API.
 * It does NOT own sockets, HTTP headers, TLS, identity or authorization.
 *
 * Strict PUT body:
 * {"requestId":uint32>0,"expectedRevision":uint32,
 *  "content":{"name":"...","title":"...","organization":"...","theme":"..."}}
 * Field order is arbitrary; duplicates, unknown keys, bad Unicode, excessive
 * depth, non-integers and trailing data are all rejected. All fields required.
 */
#define DESK_HTTP_MAX_BODY 1024u
#define DESK_HTTP_MAX_RESPONSE 1536u

typedef struct {
    uint32_t revision;
    desk_content_t content; /* NV-configured state */
    desk_content_t display; /* last successfully rendered state */
    bool hardware_validated;
} desk_http_state_t;

/* Output is only modified when the entire JSON body passes validation. */
bool desk_http_decode_set(const uint8_t *body, size_t size,
                          desk_set_command_t *command);

/*
 * Caller must capture this snapshot ON THE SERIALIZED UI OWNER TASK; do not
 * inspect desk_core_t concurrently with BLE/SLE/HTTP workers.
 *
 * Returns number of bytes excluding terminating NUL; 0 on failure.
 */
size_t desk_http_encode_status(const desk_http_state_t *state,
                               char *out, size_t capacity);
size_t desk_http_encode_reply(desk_result_t result,
                              const desk_http_state_t *state,
                              char *out, size_t capacity);

#endif
