#ifndef HAMMER_PROJECT_H
#define HAMMER_PROJECT_H
#include "hammer_extensions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_CAP_PROJECT_CONTEXT UINT64_C(4096)
/* Verified process -addon context, not the current/unsaved editor document.
   Immutable for this tools process. NULL in the picker or unknown sessions.
   Strings are borrowed until host shutdown; copy before retaining elsewhere. */
typedef struct HA_ProjectInfoV1 {
    uint32_t size, version;
    const char* addon_id;
    const char* install_root;
    const char* content_root;
    const char* game_root;
} HA_ProjectInfoV1;
typedef struct HA_ProjectV1 {
    uint32_t size, version;
    const HA_ProjectInfoV1* (HA_CALL *current)(void* context);
    /* Exact project-relative loose source file lookup, e.g. materials/foo.vmat.
       Required UTF-8 bytes incl NUL, or 0 when invalid/unavailable/not a file.
       Does not resolve mounted VPK assets or prove a dependency is missing.
       Rejects absolute paths, traversal, Windows aliases and reparse points.
       No output is written when capacity is insufficient. */
    size_t (HA_CALL *source_path)(void* context,const char* relative,char* output,size_t capacity);
} HA_ProjectV1;
static inline const HA_ProjectV1* HA_GetProject(const HA_HostV1* host) {
    const HA_ExtensionsV1* ext=HA_GetExtensions(host);
    if(!ext || ext->size<offsetof(HA_ExtensionsV1,project)+sizeof(ext->project) ||
       !ext->project || ext->project->size<sizeof(HA_ProjectV1) || ext->project->version!=1)return NULL;
    return ext->project;
}
#ifdef __cplusplus
}
#endif
#endif
