#include "hammer_editor.h"
#include <atomic>
#include <thread>
#include <stdexcept>
static const HA_HostV1* host;
static const HA_ExtensionsV1* ext;
static std::atomic<bool> entered, release;
static int HA_CALL run(void*,const HA_InteractionV1* e,char*,size_t) {
    if (e->value && e->value[0]=='b') {
        entered=true;
        while(!release) std::this_thread::yield();
        return HA_HANDLED;
    }
    throw std::runtime_error("fixture exception");
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api; ext=HA_GetExtensions(api);
    const HA_ContributionV1 c{sizeof(HA_ContributionV1),HA_COMMAND,"probe","asset_browser","Probe","","",nullptr,0,run,nullptr};
    if(!ext || !ext->register_contribution(api->context,&c)) return 0;
    if(ext->register_contribution(api->context,&c)) return 0; // duplicate ID
    auto late=c; late.id="waiting"; late.target="Late";
    auto ambiguous=c; ambiguous.id="ambiguous"; ambiguous.target="Duplicate";
    return ext->register_contribution(api->context,&late) && ext->register_contribution(api->context,&ambiguous);
}
extern "C" HA_EXPORT int HA_CALL ProbeEntered() { return entered ? 1 : 0; }
extern "C" HA_EXPORT void HA_CALL ProbeRelease() { release=true; }
extern "C" HA_EXPORT int HA_CALL ProbeLateRegistration() {
    const HA_ContributionV1 c{sizeof(HA_ContributionV1),HA_COMMAND,"late","asset_browser","Late","","",nullptr,0,run,nullptr};
    return ext->register_contribution(host->context,&c) ? 1 : 0;
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"extension_probe",HA_CAP_UI,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}

extern "C" HA_EXPORT int HA_CALL ProbePanelText(uint64_t panel,const char* control,const char* value) {
    return HA_SetPanelText(host,panel,control,value);
}
