#include "hammer_steam.h"
static const HA_HostV1* host;
static uint64_t panel,subscription;
static int delivered=0,automatic_overlay=-1;
static void HA_CALL changed(const HA_SteamStateV1*){++delivered;automatic_overlay=HA_GetSteam(host)->open_friends(host->context);}
static int HA_CALL interact(void*,const HA_InteractionV1*,char*,size_t){return HA_GetSteam(host)->open_friends(host->context)?HA_HANDLED:HA_ERROR;}
static int HA_CALL load(const HA_HostV1* api){
    host=api;auto* steam=HA_GetSteam(api);if(!steam)return 0;
    const HA_ControlV1 control{sizeof(control),HA_BUTTON,"open","Open",nullptr,nullptr};
    const HA_ContributionV1 view{sizeof(view),HA_PANEL,"probe","all","Steam probe","","",&control,1,interact,nullptr};
    panel=HA_GetExtensions(api)->register_contribution(host->context,&view);
    subscription=steam->subscribe(host->context,changed);return panel && subscription;
}
extern "C" __declspec(dllexport) const HA_HostV1* HA_CALL SteamProbeHost(){return host;}
extern "C" __declspec(dllexport) uint64_t HA_CALL SteamProbePanel(){return panel;}
extern "C" __declspec(dllexport) uint64_t HA_CALL SteamProbeSubscription(){return subscription;}
extern "C" __declspec(dllexport) int HA_CALL SteamProbeDelivered(){return delivered;}
extern "C" __declspec(dllexport) int HA_CALL SteamProbeAutomaticOverlay(){return automatic_overlay;}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi){
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"steam_probe",HA_CAP_UI|HA_CAP_STEAM,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION?&addon:nullptr;
}
