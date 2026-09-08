#include "hammer_addons.h"
#include <windows.h>
#include <cstdlib>
#include <thread>

static const HA_HostV1* host;
static unsigned events;
static void call_factory() {
    using Factory = void* (__cdecl *)(const char*, int*);
    auto factory = reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"assetbrowser.dll") ? GetModuleHandleW(L"assetbrowser.dll") : GetModuleHandleW(L"hammer.dll"), "CreateInterface"));
    if (!factory) std::abort();
    int result = -1;
    if (factory("ToolSystem2_001", &result) != reinterpret_cast<void*>(static_cast<uintptr_t>(0x12345678)) || result != 0)
        std::abort();
    if (factory("missing", &result) != nullptr || result != 1) std::abort();
}
static void nested_calls() {
    call_factory();
    // The callback waits for another thread: a thread-local guard alone deadlocks.
    std::thread worker(call_factory);
    worker.join();
}
static int HA_CALL load(const HA_HostV1* api) {
    host = api;
    nested_calls();
    host->log(host->context, "reentrant on_load passed");
    return 1;
}
static void HA_CALL event(const HA_EventV1*) {
    if (++events > 2) std::abort();
    nested_calls();
    host->log(host->context, "reentrant on_event passed");
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1), HA_ABI_VERSION, "reentrant",
        HA_CAP_LOGGING | HA_CAP_FACTORY_EVENTS, load, event, nullptr};
    return abi == HA_ABI_VERSION ? &addon : nullptr;
}
