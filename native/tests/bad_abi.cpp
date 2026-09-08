#include "hammer_addons.h"
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1), 999, "bad", 0, nullptr, nullptr, nullptr};
    return &addon;
}
