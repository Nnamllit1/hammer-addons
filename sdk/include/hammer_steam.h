#ifndef HAMMER_STEAM_H
#define HAMMER_STEAM_H
#include "hammer_extensions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_CAP_STEAM UINT64_C(16384)
#define HA_STEAM_UNAVAILABLE 0u
#define HA_STEAM_READY 1u
#define HA_STEAM_OFFLINE 2u
#define HA_STEAM_OVERLAY 1u
#define HA_STEAM_FRIENDS_TRUNCATED 2u
#define HA_STEAM_PERSONA_UNKNOWN UINT32_MAX
/* Copied snapshots, refreshed on the GUI thread roughly once per second.
   A Steam ID is an identifier, not an authentication/ownership proof. */
typedef struct HA_SteamStateV1 {
    uint32_t size, status, flags, app_id;
    uint32_t friend_count, friend_total;
    uint64_t revision, steam_id;
    char persona_name[128];
    char detail[192];
} HA_SteamStateV1;
typedef struct HA_SteamFriendV1 {
    uint32_t size, persona_state; /* Steam EPersonaState, or UNKNOWN. */
    uint64_t steam_id;
    char persona_name[128];
} HA_SteamFriendV1;
/* Borrowed snapshot during a GUI-thread callback. Copy it to retain it. */
typedef void (HA_CALL *HA_SteamCallbackFn)(const HA_SteamStateV1* state);
typedef struct HA_SteamV1 {
    uint32_t size, version;
    /* Set output.size before calling. Failure leaves output untouched.
       snapshot succeeds even when its status is unavailable/offline. */
    int (HA_CALL *snapshot)(void* context,HA_SteamStateV1* output);
    /* Pass snapshot.revision to reject a list that changed during enumeration. */
    int (HA_CALL *friend_at)(void* context,uint64_t revision,uint32_t index,HA_SteamFriendV1* output);
    /* Only from a direct user command/button callback on the GUI thread.
       Profile targets must be the current user or a cached immediate friend.
       Success means requested, not proof that Steam rendered the overlay. */
    int (HA_CALL *open_profile)(void* context,uint64_t steam_id);
    int (HA_CALL *open_friends)(void* context);
    /* Initial state on the next GUI tick, then changed snapshots only.
       Up to four subscriptions per add-on; handles belong to their add-on. */
    uint64_t (HA_CALL *subscribe)(void* context,HA_SteamCallbackFn callback);
    int (HA_CALL *unsubscribe)(void* context,uint64_t subscription);
} HA_SteamV1;
static inline const HA_SteamV1* HA_GetSteam(const HA_HostV1* host) {
    const HA_ExtensionsV1* ext=HA_GetExtensions(host);
    if(!ext || ext->size<offsetof(HA_ExtensionsV1,steam)+sizeof(ext->steam) ||
       !ext->steam || ext->steam->size<sizeof(HA_SteamV1) || ext->steam->version!=1)return NULL;
    return ext->steam;
}
#ifdef __cplusplus
}
#endif
#endif
