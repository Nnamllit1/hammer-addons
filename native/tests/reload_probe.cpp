#include "hammer_reload.h"
#include "hammer_jobs.h"
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
static const HA_HostV1* host;
static int mode,prepares,stops;
static bool active;
static std::atomic<bool> cancelled_seen{false},worker_started{false};
static int deliveries;
static void HA_CALL delivery(const HA_JobEventV1*) {++deliveries;if(mode==7)throw std::runtime_error("delivery failure");}
static void HA_CALL event(const HA_EventV1*){if(mode==6)throw std::runtime_error("event failure");}
static int HA_CALL worker(const HA_JobContextV1* context,const char*,char*,size_t){
    worker_started=true;
    while(!context->cancelled(context->context))Sleep(1);
    cancelled_seen=true;
    return 1;
}
static int HA_CALL invoke(void*,const HA_InteractionV1*,char* out,size_t capacity){
    if(mode==5){if(out)std::memset(out,'x',capacity);throw std::runtime_error("UI callback failure");}
    std::snprintf(out,capacity,"version %d",PROBE_VERSION);return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api){
    host=api;active=true;
    const HA_ContributionV1 command{sizeof(command),HA_COMMAND,"version","all","Version","","",nullptr,0,invoke,nullptr};
    const auto registered=HA_GetExtensions(host)->register_contribution(host->context,&command);
    if(PROBE_VERSION==3)throw std::runtime_error("partial initialization failure");
    return registered!=0;
}
static int HA_CALL prepare(){
    ++prepares;
    if(mode==1)return 0;
    if(mode==8){HA_GetJobs(host)->post(host->context,delivery,"vetoed work",0);return 0;}
    if(mode==9)return 2;
    if(mode==10){
        // Simulate an installer publishing unrelated new entries after staging.
        const auto root=std::filesystem::path(host->addon_directory).parent_path().parent_path().parent_path();
        std::ofstream(root/"disabled")<<"";
        std::ofstream(root/"addons/reload_probe/after-snapshot.txt")<<"new source entry";
    }
    if(mode==2)throw std::runtime_error("prepare failure");
    if(mode==4)HA_GetJobs(host)->post(host->context,delivery,"invalid retirement work",0);
    return 1;
}
static void HA_CALL shutdown(){++stops;active=false;if(mode==3)throw std::runtime_error("shutdown failure");}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi){
    static const HA_AddonV1 api{sizeof(api),HA_ABI_VERSION,"reload_probe",HA_CAP_UI|HA_CAP_SETTINGS|HA_CAP_JOBS|HA_CAP_EDITOR_QUEUE,load,event,shutdown};
    return abi==HA_ABI_VERSION?&api:nullptr;
}
extern "C" HA_EXPORT const HA_ReloadV1* HA_CALL HA_QueryReload(uint32_t version){
    static const HA_ReloadV1 api{sizeof(api),HA_RELOAD_VERSION,prepare};return version==HA_RELOAD_VERSION?&api:nullptr;
}
extern "C" HA_EXPORT int HA_CALL ProbeActive(){return active;}
extern "C" HA_EXPORT int HA_CALL ProbePrepares(){return prepares;}
extern "C" HA_EXPORT int HA_CALL ProbeStops(){return stops;}
extern "C" HA_EXPORT void HA_CALL ProbeMode(int value){mode=value;}
extern "C" HA_EXPORT int HA_CALL ProbePost(){return HA_GetJobs(host)->post(host->context,delivery,"pending",0);}
extern "C" HA_EXPORT int HA_CALL ProbeWrite(){return HA_GetExtensions(host)->set_setting(host->context,"stale","bad");}
extern "C" HA_EXPORT uint64_t HA_CALL ProbeJob(){
    const HA_JobV1 request{sizeof(request),"wait",worker,delivery,0};return HA_GetJobs(host)->submit(host->context,&request);
}
extern "C" HA_EXPORT int HA_CALL ProbeCancel(uint64_t id){return HA_GetJobs(host)->cancel(host->context,id);}

extern "C" HA_EXPORT const HA_HostV1* HA_CALL ProbeHost(){return host;}
extern "C" HA_EXPORT int HA_CALL ProbeCancelled(){return cancelled_seen;}
extern "C" HA_EXPORT int HA_CALL ProbeDeliveries(){return deliveries;}
extern "C" HA_EXPORT int HA_CALL ProbeStarted(){return worker_started;}
