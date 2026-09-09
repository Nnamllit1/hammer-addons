#include "hammer_addons.h"
static int HA_CALL load(const HA_HostV1*) { return 1; }
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"requires_factory",HA_CAP_FACTORY_EVENTS,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}
