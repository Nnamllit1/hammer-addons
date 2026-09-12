#include "hammer_reload.h"
#include "hammer_jobs.h"
#include <windows.h>
#include <cstdio>
#include <stdexcept>
static const HA_HostV1* host;
static int mode,prepares,stops;
static bool active;
static void HA_CALL delivery(const HA_JobEventV1*) {}
static int HA_CALL worker(const HA_JobContextV1* context,const char*,char*,size_t){
    while(!context->cancelled(context->context))Sleep(1);
    return 1;
}
static int HA_CALL invoke(void*,const HA_InteractionV1*,char* out,size_t capacity){
    std::snprintf(out,capacity,"version %d",PROBE_VERSION);return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api){
    host=api;active=true;
    const HA_ContributionV1 command{sizeof(command),HA_COMMAND,"version","all","Version","","",nullptr,0,invoke,nullptr};
    return HA_GetExtensions(host)->register_contribution(host->context,&command)!=0;
}
static int HA_CALL prepare(){
    ++prepares;
    if(mode==1)return 0;
    if(mode==2)throw std::runtime_error("prepare failure");
    if(mode==4)HA_GetJobs(host)->post(host->context,delivery,"invalid retirement work",0);
    return 1;
}
static void HA_CALL shutdown(){++stops;active=false;if(mode==3)throw std::runtime_error("shutdown failure");}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi){
    static const HA_AddonV1 api{sizeof(api),HA_ABI_VERSION,"reload_probe",HA_CAP_UI|HA_CAP_SETTINGS|HA_CAP_JOBS|HA_CAP_EDITOR_QUEUE,load,nullptr,shutdown};
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
