#include "steam.h"
#include "supported_tools.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
namespace ha {
namespace {
constexpr int immediate_friends=4;
constexpr size_t max_friends=256;
template<size_t N> void copy_text(char (&out)[N],const char* text) {
    if(!text)return;
    size_t size=strnlen_s(text,N);
    if(size>=N) {size=N-1;while(size && (static_cast<unsigned char>(text[size])&0xc0)==0x80)--size;}
    memcpy(out,text,size);out[size]=0;
}
bool individual(uint64_t id) {
    return (id>>56)==1 && ((id>>52)&15)==1 && ((id>>32)&0xfffff)==1 && (id&0xffffffff)!=0;
}
bool supported(HMODULE module) {
    wchar_t path[32768]{};const auto size=GetModuleFileNameW(module,path,32768);
    if(!size || size>=32768)return false;
    std::ifstream stream(std::filesystem::path(path),std::ios::binary);if(!stream)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    struct Cleanup {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
       BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)return false;
    std::array<unsigned char,65536> bytes{};
    while(stream) {stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(stream.gcount() && BCryptHashData(hash,bytes.data(),static_cast<ULONG>(stream.gcount()),0)<0)return false;}
    if(!stream.eof())return false;
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return false;
    std::string actual;constexpr char hex[]="0123456789abcdef";
    for(auto byte:digest){actual+=hex[byte>>4];actual+=hex[byte&15];}
    for(const auto* expected:supported_steam_hashes)if(actual==expected)return true;
    return false;
}
}
bool SteamExports::complete() const {
    return user_handle && pipe_handle && user && friends && utils && logged_on && user_id && persona_name &&
        friend_count && friend_id && friend_name && friend_state && app_id;
}
Steam::Steam(SteamExports api):api_(api),supplied_(api.complete()) {
    unavailable(HA_STEAM_UNAVAILABLE,"Waiting for a supported Steam session in Workshop Tools.");
}
void Steam::commit(HA_SteamStateV1 state,std::vector<HA_SteamFriendV1> friends) {
    state.size=sizeof(state);state.revision=state_.revision;
    if(memcmp(&state,&state_,sizeof(state))==0 && friends.size()==friends_.size() &&
       (friends.empty() || memcmp(friends.data(),friends_.data(),friends.size()*sizeof(HA_SteamFriendV1))==0))return;
    ++state.revision;state_=state;friends_=std::move(friends);
}
void Steam::unavailable(uint32_t status,const char* reason) {
    HA_SteamStateV1 next{};next.status=status;copy_text(next.detail,reason);commit(next,{});
}
bool Steam::resolve() {
    if(supplied_)return true;
    const auto module=GetModuleHandleW(L"steam_api64.dll");
    if(module!=module_) {
        module_=module;api_={};
        if(module && supported(module)) {
#define HA_RESOLVE(field,name) api_.field=reinterpret_cast<decltype(api_.field)>(GetProcAddress(module,name))
            HA_RESOLVE(user_handle,"SteamAPI_GetHSteamUser");
            HA_RESOLVE(pipe_handle,"SteamAPI_GetHSteamPipe");
            HA_RESOLVE(user,"SteamAPI_SteamUser_v023");
            HA_RESOLVE(friends,"SteamAPI_SteamFriends_v018");
            HA_RESOLVE(utils,"SteamAPI_SteamUtils_v010");
            HA_RESOLVE(logged_on,"SteamAPI_ISteamUser_BLoggedOn");
            HA_RESOLVE(user_id,"SteamAPI_ISteamUser_GetSteamID");
            HA_RESOLVE(persona_name,"SteamAPI_ISteamFriends_GetPersonaName");
            HA_RESOLVE(friend_count,"SteamAPI_ISteamFriends_GetFriendCount");
            HA_RESOLVE(friend_id,"SteamAPI_ISteamFriends_GetFriendByIndex");
            HA_RESOLVE(friend_name,"SteamAPI_ISteamFriends_GetFriendPersonaName");
            HA_RESOLVE(friend_state,"SteamAPI_ISteamFriends_GetFriendPersonaState");
            HA_RESOLVE(app_id,"SteamAPI_ISteamUtils_GetAppID");
            HA_RESOLVE(overlay_enabled,"SteamAPI_ISteamUtils_IsOverlayEnabled");
            HA_RESOLVE(profile,"SteamAPI_ISteamFriends_ActivateGameOverlayToUser");
            HA_RESOLVE(overlay,"SteamAPI_ISteamFriends_ActivateGameOverlay");
#undef HA_RESOLVE
        }
    }
    if(!api_.complete()) {
        unavailable(HA_STEAM_UNAVAILABLE,module ? "This Steam DLL build or its required interfaces are unsupported." : "Steam has not loaded in this tools process.");return false;
    }
    return true;
}
void Steam::refresh() noexcept {
    try {
        if(!resolve())return;
        // Borrow the host's initialized session; never Init, Shutdown or drain its callbacks.
        if(api_.user_handle()<=0 || api_.pipe_handle()<=0) {
            unavailable(HA_STEAM_UNAVAILABLE,"Workshop Tools has not initialized its Steam session.");return;
        }
        auto* user=api_.user();auto* friends=api_.friends();auto* utils=api_.utils();
        if(!user || !friends || !utils) {unavailable(HA_STEAM_UNAVAILABLE,"Required Steam interfaces are not ready.");return;}
        if(api_.app_id(utils)!=730) {unavailable(HA_STEAM_UNAVAILABLE,"Steam session is not for CS2 Workshop Tools.");return;}
        if(!api_.logged_on(user)) {unavailable(HA_STEAM_OFFLINE,"Steam is offline or the current user is not logged on.");return;}
        HA_SteamStateV1 next{};next.status=HA_STEAM_READY;next.app_id=730;next.steam_id=api_.user_id(user);
        if(!individual(next.steam_id)) {unavailable(HA_STEAM_UNAVAILABLE,"Steam returned an invalid user identity.");return;}
        copy_text(next.persona_name,api_.persona_name(friends));
        const int count=api_.friend_count(friends,immediate_friends);
        if(count<0) {unavailable(HA_STEAM_OFFLINE,"Steam friends are unavailable while logged off.");return;}
        next.friend_total=static_cast<uint32_t>(count);
        if(count>max_friends)next.flags|=HA_STEAM_FRIENDS_TRUNCATED;
        if(api_.overlay_enabled && api_.profile && api_.overlay && api_.overlay_enabled(utils))next.flags|=HA_STEAM_OVERLAY;
        std::vector<HA_SteamFriendV1> people;std::set<uint64_t> seen;
        for(int i=0;i<std::min(count,static_cast<int>(max_friends));++i) {
            HA_SteamFriendV1 person{};person.size=sizeof(person);person.steam_id=api_.friend_id(friends,i,immediate_friends);
            if(!individual(person.steam_id) || !seen.insert(person.steam_id).second)continue;
            copy_text(person.persona_name,api_.friend_name(friends,person.steam_id));
            const int state=api_.friend_state(friends,person.steam_id);
            person.persona_state=state>=0 && state<=7 ? static_cast<uint32_t>(state) : HA_STEAM_PERSONA_UNKNOWN;
            people.push_back(person);
        }
        if(api_.user_handle()<=0 || !api_.logged_on(user) || api_.user_id(user)!=next.steam_id) {
            unavailable(HA_STEAM_OFFLINE,"Steam session changed during refresh.");return;
        }
        next.friend_count=static_cast<uint32_t>(people.size());
        copy_text(next.detail,"Connected to the Workshop Tools Steam session.");commit(next,std::move(people));
    }catch(...) {unavailable(HA_STEAM_UNAVAILABLE,"Could not refresh Steam information.");}
}
bool Steam::friend_at(uint64_t revision,uint32_t index,HA_SteamFriendV1& result) const {
    if(state_.status!=HA_STEAM_READY || revision!=state_.revision || index>=friends_.size())return false;
    result=friends_[index];return true;
}
bool Steam::live(void*& friends) {
    if(state_.status!=HA_STEAM_READY || !resolve() || api_.user_handle()<=0 || api_.pipe_handle()<=0)return false;
    auto* user=api_.user();auto* utils=api_.utils();friends=api_.friends();
    return user && utils && friends && api_.logged_on(user) && api_.user_id(user)==state_.steam_id &&
        api_.app_id(utils)==730 && api_.overlay_enabled && api_.overlay_enabled(utils);
}
bool Steam::open_profile(uint64_t id) noexcept {
    try {
        if(!individual(id) || (id!=state_.steam_id && std::none_of(friends_.begin(),friends_.end(),[id](const auto& p){return p.steam_id==id;})))return false;
        void* friends=nullptr;if(!live(friends) || !api_.profile)return false;
        api_.profile(friends,"steamid",id);return true;
    }catch(...){return false;}
}
bool Steam::open_friends() noexcept {
    try {void* friends=nullptr;if(!live(friends) || !api_.overlay)return false;api_.overlay(friends,"friends");return true;}catch(...){return false;}
}
}
