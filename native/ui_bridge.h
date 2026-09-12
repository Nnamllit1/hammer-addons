#pragma once
#include <stddef.h>
#include <stdint.h>

struct HA_EditorStateV1;
struct HA_BuildOutputV1;
// Private loader/UI boundary. No Qt or C++ objects cross between these DLLs.
struct HA_UiHost {
    uint32_t size;
    void* context;
    // Returns required bytes including NUL; writes only if capacity is sufficient.
    size_t (__cdecl *read_status)(void* context, char* destination, size_t capacity);
    int (__cdecl *invoke)(void*, uint64_t, const char*, const char*, const char*, const char*, char*, size_t);
    void (__cdecl *report_binding)(void*, uint64_t, const char*, const char*);
    int (__cdecl *invoke_editor)(void*,uint64_t,const char*,const char*,const HA_EditorStateV1*);
    void (__cdecl *pump_jobs)(void*);
    void (__cdecl *editor_window)(void*,uint64_t,int);
    int (__cdecl *invoke_build)(void*,uint64_t,const char*,const HA_BuildOutputV1*);
    int (__cdecl *manage_addons)(void*,const char* action,const char* addon,char* response,size_t capacity);
};
typedef int (__cdecl *HA_UiStart)(const HA_UiHost* host);
