#ifndef DESK_HOST_CMSIS_OS2_STUB_H
#define DESK_HOST_CMSIS_OS2_STUB_H

#include <stdint.h>

typedef struct desk_test_message_queue *osMessageQueueId_t;
typedef enum {
    osOK = 0,
    osError = -1,
    osErrorResource = -3
} osStatus_t;

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id);
osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id,
                              const void *msg_ptr, uint8_t msg_prio,
                              uint32_t timeout);

#endif
