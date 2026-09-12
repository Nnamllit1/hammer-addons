#ifndef HAMMER_RELOAD_H
#define HAMMER_RELOAD_H
#include "hammer_addons.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_RELOAD_VERSION 1u
/* Export HA_QueryReload and declare reloadable=true in addon.ini.
   prepare_reload runs on the GUI thread, outside DllMain, after SDK jobs drain.
   Return 0 to veto before making destructive changes. The loader keeps this
   instance active but cannot roll back private state or SDK calls made here.
   Return 1 only after removing
   all external hooks, timers, threads and callbacks owned by this instance.
   Other return values are contract errors and disable the instance.
   Never block on the GUI thread. Do not start new work when returning 1.
   on_shutdown follows acceptance; save persistent state there or in prepare.
   Framework contributions and subscriptions are retired automatically.
   Old DLL images stay mapped until process exit; do not depend on detach events. */
typedef struct HA_ReloadV1 {
    uint32_t size,version;
    int (HA_CALL *prepare_reload)(void);
} HA_ReloadV1;
typedef const HA_ReloadV1* (HA_CALL *HA_QueryReloadFn)(uint32_t version);
#ifdef __cplusplus
}
#endif
#endif
