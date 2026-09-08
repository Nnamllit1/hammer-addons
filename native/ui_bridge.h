#pragma once
#include <stddef.h>
#include <stdint.h>

// Private loader/UI boundary. No Qt or C++ objects cross between these DLLs.
struct HA_UiHost {
    uint32_t size;
    void* context;
    // Returns required bytes including NUL; writes only if capacity is sufficient.
    size_t (__cdecl *read_status)(void* context, char* destination, size_t capacity);
};
typedef int (__cdecl *HA_UiStart)(const HA_UiHost* host);
