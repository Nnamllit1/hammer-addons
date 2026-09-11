#pragma once
#include "steam.h"
#include <stdexcept>
#include <string>
namespace steam_fixture {
inline constexpr uint64_t self=UINT64_C(76561197960265729),first=self+1;
inline bool online=true,overlay=true,ready=true,throwing=false,duplicate=false,missing_interface=false;
inline uint32_t app=730;
inline uint64_t identity=self;
inline int count=2,calls=0,profiles=0,overlays=0;
inline std::string name="Fixture mapper";
inline int handle(){return ready?1:0;}
inline void* interface(){++calls;return missing_interface?nullptr:reinterpret_cast<void*>(1);}
inline bool logged(void*){return online;}
inline uint64_t id(void*){return identity;}
inline const char* persona(void*){if(throwing)throw std::runtime_error("fixture failure");return name.c_str();}
inline int friends(void*,int flags){if(flags!=4)throw std::runtime_error("Only immediate friends expected");return count;}
inline uint64_t friend_id(void*,int index,int){return duplicate?first:first+index;}
inline const char* friend_name(void*,uint64_t){return "Fixture friend";}
inline int friend_state(void*,uint64_t id){return id==first?1:3;}
inline uint32_t app_id(void*){return app;}
inline bool has_overlay(void*){return overlay;}
inline void profile(void*,const char* dialog,uint64_t){if(std::string(dialog)!="steamid")throw std::runtime_error("Unexpected overlay route");++profiles;}
inline void friends_overlay(void*,const char* dialog){if(std::string(dialog)!="friends")throw std::runtime_error("Unexpected overlay route");++overlays;}
inline ha::SteamExports exports(){return {handle,handle,interface,interface,interface,logged,id,persona,friends,friend_id,friend_name,friend_state,app_id,has_overlay,profile,friends_overlay};}
inline void reset(){online=overlay=ready=true;throwing=duplicate=missing_interface=false;app=730;identity=self;count=2;calls=profiles=overlays=0;name="Fixture mapper";}
}
