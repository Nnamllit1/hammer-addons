#include "hammer_reload.h"
#include "hammer_editor.h"
#include "hammer_logs.h"
#include <deque>
#include <string>
static const HA_HostV1* host;
static const HA_LogsV1* logs;
static uint64_t panel,subscription;
static uint32_t minimum=HA_LOG_WARNING;
static std::deque<std::string> history;
static void HA_CALL receive(const HA_LogEventV1* event) {
    const char* severity=event->severity==HA_LOG_ERROR?"error":event->severity==HA_LOG_ASSERT?"assert":
        event->severity==HA_LOG_WARNING?"warning":"message";
    std::string text=event->text;
    if(text.size()>380) {size_t end=380;while(end && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;text.resize(end);text+="...";}
    history.push_back("["+std::string(severity)+", channel "+std::to_string(event->channel_id)+"] "+text+(event->truncated?" [capture truncated]":""));
    while(history.size()>8)history.pop_front();
    std::string output;for(const auto& line:history)output+=line+"\n";
    HA_SetPanelText(host,panel,"output",output.c_str());
    const auto status="Live output from this tools process. Buffer drops: "+std::to_string(event->dropped_total)+". Showing the latest 8 matching messages.";
    HA_SetPanelText(host,panel,"status",status.c_str());
}
static void subscribe() {
    if(subscription){logs->unsubscribe(host->context,subscription);subscription=0;}
    if(logs && logs->available(host->context))subscription=logs->subscribe(host->context,receive,minimum);
    HA_SetPanelText(host,panel,"status",subscription ? "Listening for new tool messages. Start a build or use the editor." :
        "Live tool logging is unavailable for this tools build. Saved logs can be inspected with Build log report.");
}
static int HA_CALL interact(void*,const HA_InteractionV1* event,char*,size_t) {
    const std::string control=event->control_id;
    if(control=="clear"){history.clear();HA_SetPanelText(host,panel,"output","");}
    else if(control=="filter"){minimum=std::string(event->value)=="0"?0u:HA_LOG_WARNING;subscribe();}
    else if(subscription){logs->unsubscribe(host->context,subscription);subscription=0;HA_SetPanelText(host,panel,"status","Paused. Click Pause / resume to listen again.");}
    else subscribe();
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;logs=HA_GetLogs(api);const auto* ext=HA_GetExtensions(api);if(!logs || !ext)return 0;
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"status","Status","",nullptr},
        {sizeof(HA_ControlV1),HA_CHOICE,"filter","Show","1","All messages\nWarnings and errors"},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"output","Live tool output","",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"clear","Clear",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"pause","Pause / resume",nullptr,nullptr}
    };
    const HA_ContributionV1 view{sizeof(view),HA_PANEL,"console","all","Live tool output","","",controls,5,interact,nullptr};
    panel=ext->register_contribution(host->context,&view);if(!panel)return 0;subscribe();return 1;
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"tool_console",HA_CAP_UI|HA_CAP_LIVE_PANELS|HA_CAP_TOOL_LOGS,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION?&addon:nullptr;
}

static int HA_CALL prepare_reload() {
    // All callbacks, UI bindings and subscriptions are owned by the framework.
    history.clear();
    return 1;
}
extern "C" HA_EXPORT const HA_ReloadV1* HA_CALL HA_QueryReload(uint32_t version) {
    static const HA_ReloadV1 reload{sizeof(reload),HA_RELOAD_VERSION,prepare_reload};
    return version==HA_RELOAD_VERSION ? &reload : nullptr;
}
