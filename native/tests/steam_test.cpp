#include "runtime.h"
#include "steam_fixture.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
using namespace steam_fixture;
static void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static void HA_CALL ignored(const HA_SteamStateV1*){}
int wmain(int argc,wchar_t** argv){
    try {
        check(argc==2,"pass checkout root");const fs::path root=argv[1];
        wchar_t executable[32768]{};
        check(GetModuleFileNameW(nullptr,executable,32768)>0,"locate test executable");
        const auto binaries=fs::path(executable).parent_path();
        reset();ha::Steam source(exports());
        check(source.state().status==HA_STEAM_UNAVAILABLE,"initial state unavailable");
        ready=false;source.refresh();check(calls==0,"accessors not called before host initialization");
        ready=true;missing_interface=true;source.refresh();check(source.state().status==HA_STEAM_UNAVAILABLE,"missing interface tolerated");
        missing_interface=false;source.refresh();const auto revision=source.state().revision;
        check(source.state().status==HA_STEAM_READY && source.state().steam_id==self && source.state().friend_count==2,"identity and friends captured");
        HA_SteamFriendV1 person{};check(source.friend_at(revision,1,person) && person.steam_id==first+1 && person.persona_state==3,"friend presence copied");
        source.refresh();check(source.state().revision==revision,"unchanged state coalesced");
        check(!source.friend_at(revision-1,0,person) && !source.friend_at(revision,2,person),"stale revision and invalid index rejected");
        check(source.open_profile(first) && source.open_friends() && profiles==1 && overlays==1,"restricted overlay routes requested");
        check(!source.open_profile(first+1000) && !source.open_profile(0),"unknown profile targets rejected");
        overlay=false;check(!source.open_friends(),"fresh overlay availability checked");overlay=true;
        online=false;source.refresh();check(source.state().status==HA_STEAM_OFFLINE && !source.state().steam_id && !source.state().friend_count && !source.state().persona_name[0],"logout clears personal data");
        online=true;identity=self+100;source.refresh();check(source.state().steam_id==identity && source.state().revision>revision,"account changes update identity");
        app=480;source.refresh();check(source.state().status==HA_STEAM_UNAVAILABLE && !source.open_profile(identity),"different AppID rejected");app=730;
        count=-1;source.refresh();check(source.state().status==HA_STEAM_OFFLINE,"negative friend count handled");
        count=300;source.refresh();check(source.state().friend_count==256 && source.state().friend_total==300 && (source.state().flags&HA_STEAM_FRIENDS_TRUNCATED),"large friend list bounded and reported");
        duplicate=true;source.refresh();check(source.state().friend_count==1,"duplicate friend IDs filtered");duplicate=false;
        name=std::string(126,'x')+"\xe2\x82\xac";source.refresh();check(strlen(source.state().persona_name)==126,"UTF-8 truncation keeps complete code points");
        throwing=true;source.refresh();check(source.state().status==HA_STEAM_UNAVAILABLE && source.state().steam_id==0,"provider failure clears data");reset();
        auto no_overlay=exports();no_overlay.profile=nullptr;ha::Steam limited(no_overlay);limited.refresh();
        check(limited.state().status==HA_STEAM_READY && !(limited.state().flags&HA_STEAM_OVERLAY),"optional overlay export does not disable identity");
        const auto temp=root/L"build/tests"/(L"steam-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        fs::create_directories(temp/L"addons/steam_probe");
        fs::copy_file(binaries/L"steam_probe.dll",temp/L"addons/steam_probe/steam_probe.dll");
        std::ofstream(temp/L"addons/steam_probe/addon.ini")<<"[addon]\nformat=1\nid=steam_probe\nversion=0.1.0\nabi=1\nentry=steam_probe.dll\nenabled=true\ntools=all\n";
        fs::create_directories(temp/L"addons/steam_context");
        fs::copy_file(binaries/L"steam_context.dll",temp/L"addons/steam_context/steam_context.dll");
        fs::copy_file(root/L"addons/steam_context/addon.ini",temp/L"addons/steam_context/addon.ini");
        ha::Runtime runtime(temp,temp/L"settings",false,"tools",exports());
        check(runtime.start().loaded==2,"real Steam example and API probe loaded");
        const auto module=GetModuleHandleW((temp/L"addons/steam_probe/steam_probe.dll").c_str());
        auto get_host=reinterpret_cast<const HA_HostV1*(HA_CALL*)()>(GetProcAddress(module,"SteamProbeHost"));
        auto get_panel=reinterpret_cast<uint64_t(HA_CALL*)()>(GetProcAddress(module,"SteamProbePanel"));
        auto get_sub=reinterpret_cast<uint64_t(HA_CALL*)()>(GetProcAddress(module,"SteamProbeSubscription"));
        auto delivered=reinterpret_cast<int(HA_CALL*)()>(GetProcAddress(module,"SteamProbeDelivered"));
        auto automatic=reinterpret_cast<int(HA_CALL*)()>(GetProcAddress(module,"SteamProbeAutomaticOverlay"));
        check(get_host && get_panel && get_sub && delivered && automatic,"probe exports available");
        const auto* host=get_host();const auto* api=HA_GetSteam(host);check(api!=nullptr,"Steam SDK table available");
        auto old=*host;auto ext=*host->extensions;old.extensions=&ext;ext.size=offsetof(HA_ExtensionsV1,steam);
        check(!HA_GetSteam(&old),"old host prefix rejected without reading appended pointer");
        HA_SteamStateV1 state{};state.size=sizeof(state)-1;
        check(!api->snapshot(host->context,&state) && state.size==sizeof(state)-1,"short output untouched");
        state.size=sizeof(state);check(api->snapshot(host->context,&state) && state.status==HA_STEAM_UNAVAILABLE,"startup snapshot available");
        check(!api->open_friends(host->context),"overlay rejected outside user callback");
        runtime.pump_jobs();check(delivered()==1 && automatic()==0 && overlays==0,"initial update dispatched without automatic overlay");
        check(api->snapshot(host->context,&state) && state.status==HA_STEAM_READY,"live cached SDK snapshot");
        check(runtime.status_json().find("Fixture mapper")!=std::string::npos,"real example panel updated automatically");
        check(runtime.status_json().find("s76561197960265730")!=std::string::npos,"example friend table populated");
        check(runtime.invoke(get_panel(),"hammer","panel.click","open","",nullptr,0)==HA_HANDLED && overlays==1,"direct UI action may open overlay");
        int worker=1;std::thread thread([&]{worker=api->open_friends(host->context);});thread.join();check(!worker,"worker cannot open overlay");
        runtime.pump_jobs();check(delivered()==1,"unchanged update not repeated");
        check(!api->unsubscribe(host->context,1),"cannot unsubscribe another add-on's subscription");
        const auto s1=api->subscribe(host->context,ignored),s2=api->subscribe(host->context,ignored),s3=api->subscribe(host->context,ignored);
        check(s1 && s2 && s3 && !api->subscribe(host->context,ignored),"subscription limit enforced");
        check(api->unsubscribe(host->context,get_sub()),"owned subscription removed");
        person.size=sizeof(person);const auto previous_person=person;
        check(!api->friend_at(host->context,state.revision+1,0,&person) && std::memcmp(&person,&previous_person,sizeof(person))==0,"stale API read leaves caller output untouched");
        runtime.shutdown();check(!api->snapshot(host->context,&state),"inactive addon cannot read snapshots");
        std::cout<<"Steam tests passed: readiness, offline/account changes, bounds, ABI, subscriptions, example updates and user-action gating.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
