#ifndef HAMMER_BUILD_H
#define HAMMER_BUILD_H
#include "hammer_extensions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_CAP_BUILD_OUTPUT UINT64_C(2048)
#define HA_BUILD_TRUNCATED 1u
/* Read-only snapshot of Hammer's displayed build output, not a compiler protocol.
   stream_id identifies a build dialog output control, not a compile invocation.
   Text is replaced on every update, including clears. No success is inferred.
   Callbacks run on the editor thread; all strings are borrowed UTF-8.
   At most the last 32768 UTF-16 units (131072 UTF-8 bytes) are retained.
   Coalescing may skip intermediate revisions. closed carries the last snapshot. */
typedef struct HA_BuildOutputV1 {
    uint32_t size, flags;
    uint64_t window_id, stream_id, sequence;
    const char* title;
    const char* text;
} HA_BuildOutputV1;
static inline const HA_BuildOutputV1* HA_GetBuildOutput(const HA_InteractionV1* event) {
    if(!event || event->size<offsetof(HA_InteractionV1,build)+sizeof(event->build) ||
       !event->build || event->build->size<sizeof(HA_BuildOutputV1))return NULL;
    return event->build;
}
#ifdef __cplusplus
}
#endif
#endif
