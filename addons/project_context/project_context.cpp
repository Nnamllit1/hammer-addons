#include "hammer_project.h"
#include "hammer_editor.h"
#include <cstdio>
#include <string>
#include <vector>
static const HA_HostV1* host;
static const HA_ProjectV1* project;
static uint64_t panel;
static std::string relative;
static int HA_CALL interact(void*,const HA_InteractionV1* event,char*,size_t) {
    if(std::string(event->control_id)=="path")relative=event->value;
    else if(std::string(event->control_id)=="lookup") {
        const auto needed=project->source_path(host->context,relative.c_str(),nullptr,0);
        std::string result="No project-local source file resolved. It may be packaged, outside this project, or unavailable.";
        if(needed && needed<=32768) {
            std::vector<char> buffer(needed);
            if(project->source_path(host->context,relative.c_str(),buffer.data(),buffer.size())==needed)result=buffer.data();
        }
        HA_SetPanelText(host,panel,"result",result.c_str());
    }
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;project=HA_GetProject(api);if(!project)return 0;
    const auto* info=project->current(host->context);
    std::string context=info ? std::string("Project: ")+info->addon_id+"\nContent: "+info->content_root+"\nGame: "+info->game_root :
        "Project context is unavailable in this session. Use the portable Workshop Tools launcher and select a project.";
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"context","Current project",context.c_str(),nullptr},
        {sizeof(HA_ControlV1),HA_TEXT,"path","Project-relative source","",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"lookup","Find source file",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"result","Lookup result","",nullptr}
    };
    const HA_ContributionV1 view{sizeof(view),HA_PANEL,"project","all","Project context","","",controls,4,interact,nullptr};
    panel=HA_GetExtensions(host)->register_contribution(host->context,&view);return panel!=0;
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"project_context",HA_CAP_UI|HA_CAP_LIVE_PANELS|HA_CAP_PROJECT_CONTEXT,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}
