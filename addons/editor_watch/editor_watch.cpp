#include "hammer_editor.h"
#include <deque>
#include <string>
#include <cstring>
static const HA_HostV1* host;
static uint64_t panel;
static std::deque<std::string> history;
static int HA_CALL interact(void*,const HA_InteractionV1* event,char*,size_t) {
    if(const auto* state=HA_GetEditorState(event)) {
        std::string line=std::string(event->phase)+" ["+event->tool+"]\n"+
            state->session_id+"/"+std::to_string(state->window_id)+" #"+std::to_string(state->sequence)+"\n"+
            state->title+"\nDocument: "+(state->flags&HA_EDITOR_HAS_DOCUMENT_PATH ? state->document_path : "(not reported by editor)")+
            "\nQt window modified indicator: "+(state->flags&HA_EDITOR_WINDOW_MODIFIED ? "on" : "off")+"\n";
        if(line.size()>900) {size_t end=900;while((static_cast<unsigned char>(line[end])&0xc0)==0x80)--end;line.resize(end);}
        history.push_back(line);
        if(history.size()>4)history.pop_front();
    } else if(std::strcmp(event->control_id,"clear")==0) history.clear();
    std::string text;
    for(const auto& line:history)text+=line+"\n";
    return HA_SetPanelText(host,panel,"events",text.c_str()) ? HA_HANDLED : HA_ERROR;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;const auto* ext=HA_GetExtensions(api);if(!ext)return 0;
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"scope","Scope","Window metadata only; no scene operations or save notifications.",nullptr},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"events","Recent observations","Waiting for an editor...",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"clear","Clear history",nullptr,nullptr}
    };
    const HA_ContributionV1 view{sizeof(HA_ContributionV1),HA_PANEL,"history","all",
        "Editor observations","","",controls,3,interact,nullptr};
    panel=ext->register_contribution(host->context,&view);
    const HA_ContributionV1 observer{sizeof(HA_ContributionV1),HA_EDITOR_OBSERVER,"watch","all",
        "Editor lifecycle","","",nullptr,0,interact,nullptr};
    return panel && ext->register_contribution(host->context,&observer);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(HA_AddonV1),HA_ABI_VERSION,"editor_watch",
        HA_CAP_UI|HA_CAP_EDITOR_EVENTS|HA_CAP_LIVE_PANELS,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}
