#ifndef HAMMER_ADDONS_H
#define HAMMER_ADDONS_H
#include <stdint.h>

/* ABI v1: C layout, UTF-8 strings, no C++ objects or ownership across DLLs.
   All strings passed to callbacks are borrowed for the duration of the call.
   Add-ons are trusted native code, not sandboxed. */
#ifdef __cplusplus
extern "C" {
#endif
#define HA_ABI_VERSION 1u
#define HA_EXPORT __declspec(dllexport)
#define HA_CALL __cdecl
#define HA_CAP_LOGGING UINT64_C(1)
#define HA_CAP_FACTORY_EVENTS UINT64_C(2)

struct HA_ExtensionsV1;

typedef struct HA_HostV1 {
    uint32_t size;
    uint32_t abi_version;
    uint64_t capabilities;
    void* context;
    void (HA_CALL *log)(void* context, const char* message);
    const char* addon_directory;
    /* Optional appended extension table; check size via HA_GetExtensions. */
    const struct HA_ExtensionsV1* extensions;
} HA_HostV1;

typedef struct HA_EventV1 {
    uint32_t size;
    const char* name;
    const char* value;
} HA_EventV1;

typedef struct HA_AddonV1 {
    uint32_t size;
    uint32_t abi_version;
    const char* id;
    uint64_t required_capabilities;
    /* Return 1 on success. A failed start must clean up its own resources. */
    int (HA_CALL *on_load)(const HA_HostV1* host);
    void (HA_CALL *on_event)(const HA_EventV1* event);
    /* Called only on explicit host shutdown, never under Windows loader lock.
       Editor process termination does not guarantee this callback. */
    void (HA_CALL *on_shutdown)(void);
} HA_AddonV1;

typedef const HA_AddonV1* (HA_CALL *HA_QueryFn)(uint32_t requested_abi);
/* Every add-on exports: const HA_AddonV1* HA_CALL HA_Query(uint32_t); */
#ifdef __cplusplus
}
#endif
#endif
