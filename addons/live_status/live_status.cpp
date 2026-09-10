#include "hammer_editor.h"
#include <cstdio>
static const HA_HostV1* host;
static uint64_t panel;
static unsigned count;
static int HA_CALL interact(void*,const HA_InteractionV1*,char*,size_t) {
    char text[128]{};
    std::snprintf(text,sizeof(text),"Session counter: %u",++count);
    return HA_SetPanelText(host,panel,"count",text) ? HA_HANDLED : HA_ERROR;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;const auto* ext=HA_GetExtensions(api);if(!ext)return 0;
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"count","Count","Session counter: 0",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"increment","Increment",nullptr,nullptr}
    };
    const HA_ContributionV1 view{sizeof(HA_ContributionV1),HA_PANEL,"counter","all",
        "Live panel example","","",controls,2,interact,nullptr};
    panel=ext->register_contribution(host->context,&view);
    return panel!=0;
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"live_status",
        HA_CAP_UI|HA_CAP_LIVE_PANELS,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}
