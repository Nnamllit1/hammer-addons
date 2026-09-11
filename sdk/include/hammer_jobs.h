#ifndef HAMMER_JOBS_H
#define HAMMER_JOBS_H
#include "hammer_extensions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_CAP_JOBS UINT64_C(256)
#define HA_CAP_EDITOR_QUEUE UINT64_C(512)
#define HA_JOB_PROGRESS 1u
#define HA_JOB_MESSAGE 2u
#define HA_JOB_SUCCEEDED 3u
#define HA_JOB_FAILED 4u
#define HA_JOB_CANCELLED 5u
typedef struct HA_JobEventV1 {
    uint32_t size, state, progress; /* progress: 0..100, advisory */
    uint64_t job_id; /* 0 for a standalone posted callback */
    const char* text; /* borrowed UTF-8 during the callback */
} HA_JobEventV1;
typedef void (HA_CALL *HA_EditorCallbackFn)(const HA_JobEventV1* event);
typedef struct HA_JobContextV1 {
    uint32_t size;
    void* context;
    int (HA_CALL *cancelled)(void*);
    int (HA_CALL *report)(void*, uint32_t progress, const char* text);
    int (HA_CALL *post)(void*, const char* text);
} HA_JobContextV1;
/* Worker thread: use only owned local data, copied input and this job context.
   Do not use Qt, editor objects, host APIs or add-on globals freed on shutdown.
   Return 1 on success, 0 on failure; write a NUL-terminated UTF-8 result.
   Check cancellation regularly. Context/input expire when the worker returns. */
typedef int (HA_CALL *HA_JobWorkFn)(const HA_JobContextV1*, const char* input, char* result, size_t capacity);
typedef struct HA_JobV1 {
    uint32_t size;
    const char* input; /* copied UTF-8, <=32768 bytes */
    HA_JobWorkFn work;
    HA_EditorCallbackFn notify; /* editor thread, serialized with other callbacks */
    uint64_t window_id; /* 0: process scope; otherwise a currently observed window */
} HA_JobV1;
typedef struct HA_JobsV1 {
    uint32_t size, version;
    uint64_t (HA_CALL *submit)(void* host_context, const HA_JobV1*);
    int (HA_CALL *cancel)(void* host_context, uint64_t job_id);
    int (HA_CALL *post)(void* host_context, HA_EditorCallbackFn, const char* text, uint64_t window_id);
} HA_JobsV1;
/* Requires the appended extension field; old ABI-1 hosts remain valid. */
static inline const HA_JobsV1* HA_GetJobs(const HA_HostV1* host) {
    const HA_ExtensionsV1* ext=HA_GetExtensions(host);
    if (!ext || ext->size < offsetof(HA_ExtensionsV1,jobs)+sizeof(ext->jobs) ||
        !ext->jobs || ext->jobs->size < sizeof(HA_JobsV1) || ext->jobs->version != 1) return NULL;
    return ext->jobs;
}
#ifdef __cplusplus
}
#endif
#endif
