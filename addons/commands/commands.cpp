#include "hammer_reload.h"
#include "hammer_extensions.h"
#include <cstdio>
static int HA_CALL run(void*, const HA_InteractionV1* event, char* response, size_t capacity) {
    std::snprintf(response,capacity,"Command example executed in %s.",event->tool);
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* host) {
    const auto* ext=HA_GetExtensions(host);
    if (!ext) return 0;
    const HA_ContributionV1 command{sizeof(HA_ContributionV1),HA_COMMAND,"greet","asset_browser",
        "Example: greet","Help","Ctrl+Alt+G",nullptr,0,run,nullptr};
    return ext->register_contribution(host->context,&command) != 0;
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"commands",HA_CAP_UI,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}

static int HA_CALL prepare_reload() {
    // All callbacks, UI bindings and subscriptions are owned by the framework.
    return 1;
}
extern "C" HA_EXPORT const HA_ReloadV1* HA_CALL HA_QueryReload(uint32_t version) {
    static const HA_ReloadV1 reload{sizeof(reload),HA_RELOAD_VERSION,prepare_reload};
    return version==HA_RELOAD_VERSION ? &reload : nullptr;
}
