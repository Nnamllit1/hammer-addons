#include "hammer_reload.h"
#include "hammer_extensions.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
static int HA_CALL import_note(void*,const HA_InteractionV1* event,char* response,size_t capacity) {
    // Deliberately a tiny custom text format; this is not a model converter.
    const auto* chars=reinterpret_cast<const char8_t*>(event->value);
    const std::filesystem::path path{std::u8string(chars)};
    if(std::filesystem::file_size(path)>4096) {
        std::snprintf(response,capacity,"Example notes are limited to 4 KiB.");
        return HA_ERROR;
    }
    std::ifstream stream(path);
    std::string magic, text;
    std::getline(stream,magic);
    if(magic!="HAMMER_ADDONS_NOTE") {
        std::snprintf(response,capacity,"Not an example note file.");
        return HA_ERROR;
    }
    std::getline(stream,text);
    std::snprintf(response,capacity,"Note: %s",text.c_str());
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    const auto* ext=HA_GetExtensions(api);
    if(!ext) return 0;
    const HA_ContributionV1 importer{sizeof(HA_ContributionV1),HA_IMPORTER,"note","asset_browser",
        "Example: open note file","File","hanote",nullptr,0,import_note,nullptr};
    // An import route extends an existing action, retaining its built-in importer.
    // File/Import is exercised by the integration fixture, not yet verified in native ModelDoc.
    // Missing targets stay waiting; this does not advertise native model conversion.
    const HA_ContributionV1 route{sizeof(HA_ContributionV1),HA_IMPORT_ROUTE,"model_note","modeldoc",
        "Example note reader","File/Import","hanote",nullptr,0,import_note,nullptr};
    return ext->register_contribution(api->context,&importer) && ext->register_contribution(api->context,&route);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"note_import",
        HA_CAP_UI|HA_CAP_IMPORTERS,load,nullptr,nullptr};
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
