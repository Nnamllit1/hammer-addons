#include "hammer_steam.h"
#include "hammer_editor.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
static const HA_HostV1* host;
static const HA_SteamV1* steam;
static uint64_t panel,selected;
static size_t page;
static HA_SteamStateV1 state{};
static std::vector<HA_SteamFriendV1> friends;
static std::string cell(const char* value) {
    std::string text=value;for(char& c:text)if(static_cast<unsigned char>(c)<32)c=' ';return text;
}
static const char* presence(uint32_t value) {
    static const char* names[]{"Offline","Online","Busy","Away","Snooze","Looking to trade","Looking to play","Invisible"};
    return value<std::size(names)?names[value]:"Unknown";
}
static void render() {
    const auto pages=std::max<size_t>(1,(friends.size()+15)/16);page=std::min(page,pages-1);
    std::string summary=state.detail;
    if(state.status==HA_STEAM_READY)summary=cell(state.persona_name)+"\nSteam ID: "+std::to_string(state.steam_id)+
        "\nApp: "+std::to_string(state.app_id)+" | "+std::to_string(state.friend_count)+" / "+std::to_string(state.friend_total)+" friends cached"+
        ((state.flags&HA_STEAM_OVERLAY)?"\nSteam overlay available.":"\nSteam overlay unavailable in this window/session.");
    if(state.flags&HA_STEAM_FRIENDS_TRUNCATED)summary+="\nOnly the first 256 friends are cached.";
    std::string rows;
    for(size_t i=page*16;i<std::min(friends.size(),(page+1)*16);++i) {
        const auto& person=friends[i];const auto id=std::to_string(person.steam_id);
        rows+="s"+id+"\t"+cell(person.persona_name)+"\t"+presence(person.persona_state)+"\t"+id+"\n";
    }
    std::string details="Select a friend to open their Steam profile.";
    if(auto it=std::find_if(friends.begin(),friends.end(),[](const auto& p){return p.steam_id==selected;});it!=friends.end())
        details=cell(it->persona_name)+" | Steam ID: "+std::to_string(selected);
    else selected=0;
    HA_SetPanelText(host,panel,"account",summary.c_str());
    HA_SetPanelText(host,panel,"friends",rows.c_str());
    HA_SetPanelText(host,panel,"page",("Page "+std::to_string(page+1)+" / "+std::to_string(pages)).c_str());
    HA_SetPanelText(host,panel,"selected",details.c_str());
}
static void HA_CALL changed(const HA_SteamStateV1* update) {
    if(!update || update->size<sizeof(*update))return;
    std::vector<HA_SteamFriendV1> next;
    for(uint32_t i=0;i<update->friend_count;++i) {
        HA_SteamFriendV1 person{};person.size=sizeof(person);
        if(!steam->friend_at(host->context,update->revision,i,&person))return;
        next.push_back(person);
    }
    state=*update;friends=std::move(next);render();
}
static int HA_CALL interact(void*,const HA_InteractionV1* event,char* response,size_t capacity) {
    const std::string control=event->control_id;
    if(control=="friends") {
        const std::string key=event->value;
        selected=0;
        for(const auto& person:friends)if("s"+std::to_string(person.steam_id)==key)selected=person.steam_id;
    } else if(control=="next") {++page;selected=0;}
    else if(control=="previous") {if(page)--page;selected=0;}
    else {
        int requested=0;
        if(control=="mine")requested=steam->open_profile(host->context,state.steam_id);
        else if(control=="profile")requested=steam->open_profile(host->context,selected);
        else if(control=="overlay")requested=steam->open_friends(host->context);
        if(!requested) {std::snprintf(response,capacity,"Steam overlay is unavailable, or no current profile is selected.");return HA_ERROR;}
        std::snprintf(response,capacity,"Requested the Steam overlay.");return HA_HANDLED;
    }
    render();return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;steam=HA_GetSteam(api);if(!steam)return 0;
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"account","Steam account","Waiting for the Workshop Tools Steam session...",nullptr},
        {sizeof(HA_ControlV1),HA_TABLE,"friends","Friends","","Name\tPresence\tSteam ID"},
        {sizeof(HA_ControlV1),HA_LABEL,"page","Page","",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"previous","Previous page",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"next","Next page",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_LABEL,"selected","Selected friend","Select a friend.",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"profile","Open selected Steam profile",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"mine","Open my Steam profile",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"overlay","Open Steam friends",nullptr,nullptr}
    };
    const HA_ContributionV1 view{sizeof(view),HA_PANEL,"steam","all","Steam account and friends","","",controls,static_cast<uint32_t>(std::size(controls)),interact,nullptr};
    panel=HA_GetExtensions(host)->register_contribution(host->context,&view);
    return panel && steam->subscribe(host->context,changed);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"steam_context",HA_CAP_UI|HA_CAP_LIVE_PANELS|HA_CAP_TABLES|HA_CAP_STEAM,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION?&addon:nullptr;
}
