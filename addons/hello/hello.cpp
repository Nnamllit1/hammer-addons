#include "hammer_reload.h"
#include "hammer_addons.h"
#include <string>
static const HA_HostV1* host;
static int HA_CALL load(const HA_HostV1* api) {
    if (!api || api->size < sizeof(HA_HostV1) || api->abi_version != HA_ABI_VERSION) return 0;
    host = api;
    host->log(host->context, "Hello from a native Hammer add-on!");
    return 1;
}
static void HA_CALL event(const HA_EventV1* e) {
    auto text = std::string(e->name) + ": " + e->value;
    host->log(host->context, text.c_str());
}
static void HA_CALL shutdown() { host->log(host->context, "shutdown"); }
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1), HA_ABI_VERSION, "hello", HA_CAP_LOGGING, load, event, shutdown};
    return abi == HA_ABI_VERSION ? &addon : nullptr;
}

static int HA_CALL prepare_reload() {
    // All callbacks, UI bindings and subscriptions are owned by the framework.
    return 1;
}
extern "C" HA_EXPORT const HA_ReloadV1* HA_CALL HA_QueryReload(uint32_t version) {
    static const HA_ReloadV1 reload{sizeof(reload),HA_RELOAD_VERSION,prepare_reload};
    return version==HA_RELOAD_VERSION ? &reload : nullptr;
}
