#ifndef XIAOHONG_DESK_CORE_H
#define XIAOHONG_DESK_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* Buffer sizes include the terminating NUL; UTF-8 text is byte-limited. */
#define DESK_NAME_CAP 49u
#define DESK_TITLE_CAP 65u
#define DESK_ORG_CAP 97u
#define DESK_THEME_CAP 17u

typedef struct {
    char name[DESK_NAME_CAP];
    char title[DESK_TITLE_CAP];
    char organization[DESK_ORG_CAP];
    char theme[DESK_THEME_CAP];
} desk_content_t;

typedef struct {
    uint32_t request_id;
    uint32_t expected_revision;
    desk_content_t content;
} desk_set_command_t;

typedef enum {
    DESK_OK = 0,
    DESK_BAD_INPUT,
    DESK_UNAUTHORIZED,
    DESK_CONFLICT,
    DESK_DUPLICATE_ID,
    DESK_STORE_ERROR,
    DESK_RENDER_ERROR,
    DESK_REVISION_EXHAUSTED
} desk_result_t;

/* 1 for success; 0 for unavailable/failure. callbacks run in the Desk UI task. */
typedef struct {
    int (*load)(void *user, desk_content_t *out, uint32_t *revision);
    int (*save)(void *user, const desk_content_t *value, uint32_t revision);
    int (*render)(void *user, const desk_content_t *value);
    void *user;
} desk_hooks_t;

typedef struct {
    desk_content_t content;
    uint32_t revision;
    uint32_t last_request_id;
    uint32_t last_request_revision;
    uint32_t last_expected_revision;
    bool has_last_request;
    desk_hooks_t hooks;
} desk_core_t;

/* Initialize core and load an existing configuration if available and valid. */
void desk_core_init(desk_core_t *core, desk_hooks_t hooks);
/* Call only from a serialized owner/UI task; radio callbacks should enqueue. */
desk_result_t desk_core_apply(desk_core_t *core, const desk_set_command_t *command,
                               bool authorized);
desk_result_t desk_core_repaint(desk_core_t *core);
bool desk_content_valid(const desk_content_t *content);
const char *desk_result_name(desk_result_t result);

#endif
