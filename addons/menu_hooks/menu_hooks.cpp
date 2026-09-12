#include "hammer_reload.h"
#include "hammer_extensions.h"
#include <cstdio>
#include <cstring>
static bool handle_action=false;
static const HA_HostV1* host;
static int HA_CALL interact(void*,const HA_InteractionV1* event,char* response,size_t capacity) {
    if (std::strcmp(event->phase,"panel.change")==0) handle_action=std::strcmp(event->value,"1")==0;
    if (std::strcmp(event->phase,"hook.before")==0) {
        host->log(host->context,"before About");
        if(handle_action) {
            std::snprintf(response,capacity,"The example handled About. Uncheck the option to restore normal behavior.");
            return HA_HANDLED;
        }
        return HA_CONTINUE;
    }
    if (std::strcmp(event->phase,"hook.after")==0) host->log(host->context,"after About returned");
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;
    const auto* ext=HA_GetExtensions(api);
    if(!ext) return 0;
    const HA_ControlV1 control{sizeof(HA_ControlV1),HA_CHECKBOX,"handle","Handle About in this example","0",nullptr};
    const HA_ContributionV1 panel{sizeof(HA_ContributionV1),HA_PANEL,"options","asset_browser",
        "Example: menu hook options","","",&control,1,interact,nullptr};
    const HA_ContributionV1 hook{sizeof(HA_ContributionV1),HA_MENU_HOOK,"about","asset_browser",
        "About hook","Help/About","",nullptr,0,interact,nullptr};
    return ext->register_contribution(api->context,&panel) && ext->register_contribution(api->context,&hook);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"menu_hooks",
        HA_CAP_UI|HA_CAP_MENU_HOOKS,load,nullptr,nullptr};
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
