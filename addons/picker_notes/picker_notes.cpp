#include "hammer_reload.h"
#include "hammer_extensions.h"
#include <cstdio>
#include <cstring>
static const HA_HostV1* host;
static const HA_ExtensionsV1* ext;
static int HA_CALL interact(void*,const HA_InteractionV1* event,char* response,size_t capacity) {
    if (std::strcmp(event->phase,"panel.change")==0) {
        if (!ext->set_setting(host->context,event->control_id,event->value)) {
            std::snprintf(response,capacity,"Could not save this setting.");
            return HA_ERROR;
        }
        std::snprintf(response,capacity,"Saved %s.",event->control_id);
    } else {
        char name[256]{};
        ext->get_setting(host->context,"name",name,sizeof(name));
        std::snprintf(response,capacity,"Reminder: %s",*name ? name : "No reminder saved yet");
    }
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api; ext=HA_GetExtensions(api);
    if(!ext) return 0;
    char name[256]{}, enabled[8]{}, mode[8]{};
    ext->get_setting(host->context,"name",name,sizeof(name));
    ext->get_setting(host->context,"enabled",enabled,sizeof(enabled));
    ext->get_setting(host->context,"mode",mode,sizeof(mode));
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"intro","Notes","Keep a reminder before choosing your Workshop project.",nullptr},
        {sizeof(HA_ControlV1),HA_TEXT,"name","Reminder",name,nullptr},
        {sizeof(HA_ControlV1),HA_CHECKBOX,"enabled","Example preference",std::strcmp(enabled,"1")==0 ? "1" : "0",nullptr},
        {sizeof(HA_ControlV1),HA_CHOICE,"mode","Mode",std::strcmp(mode,"1")==0 ? "1" : "0","Simple\nDetailed"},
        {sizeof(HA_ControlV1),HA_BUTTON,"greet","Read reminder",nullptr,nullptr}
    };
    const HA_ContributionV1 panel{sizeof(HA_ContributionV1),HA_PANEL,"preferences","project_picker",
        "Project picker notes","","",controls,5,interact,nullptr};
    return ext->register_contribution(host->context,&panel)!=0;
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"picker_notes",
        HA_CAP_UI|HA_CAP_SETTINGS,load,nullptr,nullptr};
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
