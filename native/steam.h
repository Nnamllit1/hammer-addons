#pragma once
#include "hammer_steam.h"
#include <windows.h>
#include <vector>

namespace ha {
// Narrow declarations of Valve's documented flat C API. Never expose an engine
// interface pointer, authentication ticket, inventory or trading API to add-ons.
struct SteamExports {
    int (__cdecl *user_handle)()=nullptr;
    int (__cdecl *pipe_handle)()=nullptr;
    void* (__cdecl *user)()=nullptr;
    void* (__cdecl *friends)()=nullptr;
    void* (__cdecl *utils)()=nullptr;
    bool (__cdecl *logged_on)(void*)=nullptr;
    uint64_t (__cdecl *user_id)(void*)=nullptr;
    const char* (__cdecl *persona_name)(void*)=nullptr;
    int (__cdecl *friend_count)(void*,int)=nullptr;
    uint64_t (__cdecl *friend_id)(void*,int,int)=nullptr;
    const char* (__cdecl *friend_name)(void*,uint64_t)=nullptr;
    int (__cdecl *friend_state)(void*,uint64_t)=nullptr;
    uint32_t (__cdecl *app_id)(void*)=nullptr;
    bool (__cdecl *overlay_enabled)(void*)=nullptr;
    void (__cdecl *profile)(void*,const char*,uint64_t)=nullptr;
    void (__cdecl *overlay)(void*,const char*)=nullptr;
    bool complete() const;
};
class Steam {
    SteamExports api_;
    bool supplied_=false;
    HMODULE module_=nullptr;
    HA_SteamStateV1 state_{};
    std::vector<HA_SteamFriendV1> friends_;
    bool resolve();
    void unavailable(uint32_t status,const char* reason);
    void commit(HA_SteamStateV1 state,std::vector<HA_SteamFriendV1> friends);
    bool live(void*& friends);
public:
    // A supplied backend supports isolated tests without Steam or network calls.
    explicit Steam(SteamExports api={});
    void refresh() noexcept;
    const HA_SteamStateV1& state() const {return state_;}
    bool friend_at(uint64_t revision,uint32_t index,HA_SteamFriendV1& result) const;
    bool open_profile(uint64_t id) noexcept;
    bool open_friends() noexcept;
};
}
