#include "hammer_jobs.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
static const HA_HostV1* host;
static const HA_JobsV1* jobs;
static unsigned calls, terminal;
static DWORD ui;
static bool wrong;
static char last[4097];
static void HA_CALL notify(const HA_JobEventV1* event) {
    wrong|=GetCurrentThreadId()!=ui;++calls;
    if(event->state>=HA_JOB_SUCCEEDED)terminal=event->state;
    strcpy_s(last,event->text);
    if(std::strcmp(event->text,"throw_callback")==0)throw std::runtime_error("queued failure");
    if(std::strcmp(event->text,"repost")==0)jobs->post(host->context,notify,"next tick",0);
}
static int HA_CALL worker(const HA_JobContextV1* context,const char* input,char* result,size_t size) {
    if(std::strcmp(input,"throw_worker")==0)throw std::runtime_error("worker failure");
    if(std::strcmp(input,"wait")==0)while(!context->cancelled(context->context))Sleep(1);
    else {
        context->post(context->context,"worker message");
        context->report(context->context,50,"halfway");
        Sleep(10);
    }
    std::snprintf(result,size,"%s",input);return 1;
}
static int HA_CALL load(const HA_HostV1* api) {host=api;jobs=HA_GetJobs(api);return jobs!=nullptr;}
extern "C" HA_EXPORT void HA_CALL ProbeReset() {calls=terminal=0;wrong=false;last[0]=0;ui=GetCurrentThreadId();}
extern "C" HA_EXPORT unsigned HA_CALL ProbeCalls() {return calls;}
extern "C" HA_EXPORT unsigned HA_CALL ProbeTerminal() {return terminal;}
extern "C" HA_EXPORT int HA_CALL ProbeWrongThread() {return wrong;}
extern "C" HA_EXPORT const char* HA_CALL ProbeLast() {return last;}
extern "C" HA_EXPORT uint64_t HA_CALL ProbeSubmit(const char* text,uint64_t window) {
    const HA_JobV1 request{sizeof(request),text,worker,notify,window};return jobs->submit(host->context,&request);
}
extern "C" HA_EXPORT int HA_CALL ProbePost(const char* text,uint64_t window) {return jobs->post(host->context,notify,text,window);}
extern "C" HA_EXPORT int HA_CALL ProbeCancel(uint64_t id) {return jobs->cancel(host->context,id);}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"jobs_probe",HA_CAP_JOBS|HA_CAP_EDITOR_QUEUE,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION?&addon:nullptr;
}
