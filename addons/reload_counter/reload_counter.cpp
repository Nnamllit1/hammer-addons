#include "hammer_reload.h"
#include "hammer_editor.h"
#include <cstdio>
#include <cstdlib>
static const HA_HostV1* host;
static uint64_t panel;
static unsigned count;
static int HA_CALL click(void*,const HA_InteractionV1*,char*,size_t){
    char text[64]{};std::snprintf(text,sizeof(text),"Count: %u",++count);HA_SetPanelText(host,panel,"count",text);return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api){
    host=api;const auto* ext=HA_GetExtensions(api);if(!ext)return 0;
    char saved[32]{};if(ext->get_setting(host->context,"count",saved,sizeof(saved)))count=static_cast<unsigned>(std::strtoul(saved,nullptr,10));
    char text[64]{};std::snprintf(text,sizeof(text),"Count: %u",count);
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"count","Count",text,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"increment","Increment",nullptr,nullptr}
    };
    const HA_ContributionV1 definition{sizeof(definition),HA_PANEL,"counter","all","Reload counter","","",controls,2,click,nullptr};
    panel=ext->register_contribution(host->context,&definition);return panel!=0;
}
static int HA_CALL prepare(){
    // No private threads, hooks or external callbacks. Persist before retirement.
    char saved[32]{};std::snprintf(saved,sizeof(saved),"%u",count);
    return HA_GetExtensions(host)->set_setting(host->context,"count",saved);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi){
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"reload_counter",HA_CAP_UI|HA_CAP_SETTINGS|HA_CAP_LIVE_PANELS,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION?&addon:nullptr;
}
extern "C" HA_EXPORT const HA_ReloadV1* HA_CALL HA_QueryReload(uint32_t version){
    static const HA_ReloadV1 reload{sizeof(reload),HA_RELOAD_VERSION,prepare};return version==HA_RELOAD_VERSION?&reload:nullptr;
}
