#ifndef HAMMER_LOGS_H
#define HAMMER_LOGS_H
#include "hammer_extensions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_CAP_TOOL_LOGS UINT64_C(1024)
#define HA_LOG_DETAILED 1u
#define HA_LOG_MESSAGE 2u
#define HA_LOG_WARNING 3u
#define HA_LOG_ASSERT 4u
#define HA_LOG_ERROR 5u
/* Observed output in this process, not compiler stdin or a complete build log.
   Called on the editor thread. Text is borrowed UTF-8; copy it to retain it. */
typedef struct HA_LogEventV1 {
    uint32_t size, severity;
    int32_t channel_id;
    uint32_t truncated;
    uint64_t sequence, dropped_total;
    const char* text;
} HA_LogEventV1;
typedef void (HA_CALL *HA_LogCallbackFn)(const HA_LogEventV1*);
typedef struct HA_LogsV1 {
    uint32_t size, version;
    int (HA_CALL *available)(void* host_context);
    uint64_t (HA_CALL *subscribe)(void* host_context, HA_LogCallbackFn, uint32_t minimum_severity);
    int (HA_CALL *unsubscribe)(void* host_context, uint64_t subscription);
} HA_LogsV1;
static inline const HA_LogsV1* HA_GetLogs(const HA_HostV1* host) {
    const HA_ExtensionsV1* ext=HA_GetExtensions(host);
    if(!ext || ext->size<offsetof(HA_ExtensionsV1,logs)+sizeof(ext->logs) || !ext->logs ||
       ext->logs->size<sizeof(HA_LogsV1) || ext->logs->version!=1)return NULL;
    return ext->logs;
}
#ifdef __cplusplus
}
#endif
#endif
