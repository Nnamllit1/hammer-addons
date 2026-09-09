#include "runtime.h"
#include "ui_bridge.h"
#include <shellapi.h>
#include <atomic>
#include <cstring>
namespace fs=std::filesystem;
static HMODULE self;
static std::atomic<bool> started{false};
static ha::Runtime* runtime=nullptr;
static size_t __cdecl read(void*,char* output,size_t capacity) {
    try {
        const auto text=runtime->status_json();
        if(text.empty()) return 0;
        if(output && capacity>=text.size()+1) std::memcpy(output,text.c_str(),text.size()+1);
        return text.size()+1;
    } catch(...) {return 0;}
}
static int __cdecl invoke(void*,uint64_t handle,const char* tool,const char* phase,const char* control,const char* value,char* response,size_t capacity) {
    try{return runtime->invoke(handle,tool,phase,control,value,response,capacity);}catch(...){return HA_ERROR;}
}
static void __cdecl report(void*,uint64_t handle,const char* tool,const char* message) {
    try{runtime->report_binding(handle,tool,message);}catch(...){}
}
static bool picker_session=false;
static bool tools_session() {
    wchar_t executable[32768]{};
    if(!GetModuleFileNameW(nullptr,executable,32768)) return false;
    const auto name=fs::path(executable).filename();
    picker_session=_wcsicmp(name.c_str(),L"csgocfg.exe")==0;
    if(!picker_session && _wcsicmp(name.c_str(),L"cs2.exe")) return false;
    int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args) return false;
    bool tools=false,insecure=false;
    for(int i=1;i<count;++i) {
        tools|=wcscmp(args[i],L"-tools")==0;
        insecure|=wcscmp(args[i],L"-insecure")==0;
    }
    LocalFree(args);
    return insecure && (tools || picker_session);
}
// Explicit initialization after LoadLibrary returns; DllMain performs no startup work.
extern "C" __declspec(dllexport) DWORD WINAPI HA_StartTools(void*) noexcept {
    try {
        if(!tools_session()) return 2;
        if(started.exchange(true)) return 3;
        wchar_t path[32768]{};
        if(!GetModuleFileNameW(self,path,32768)) return 4;
        const auto root=fs::path(path).parent_path();
        if(!ha::plain_path(root)) return 4;
        // This entry point does not intercept Valve's factory, so do not advertise observations.
        runtime=new ha::Runtime(root,{},false,picker_session ? "project_picker" : "tools");
        runtime->log(picker_session ? "Starting launcher-owned Workshop project picker" : "Starting launcher-owned insecure Workshop Tools session");
        for(unsigned attempt=0;attempt<120;++attempt) {
            if((picker_session || GetModuleHandleW(L"assetbrowser.dll")) && GetModuleHandleW(L"Qt5Widgets.dll")) {
                const auto ui=root/L"hammer_addons_ui.dll";
                if(!ha::plain_path(ui)) return 5;
                auto module=LoadLibraryExW(ui.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
                if(!module) {runtime->log("Cannot load UI DLL: "+std::to_string(GetLastError()));return 5;}
                auto start=reinterpret_cast<HA_UiStart>(GetProcAddress(module,"HA_StartUi"));
                if(!start) return 5;
                try{runtime->start();}catch(const std::exception& e){runtime->initialization_error(e.what());}
                static const HA_UiHost host{sizeof(HA_UiHost),nullptr,read,invoke,report};
                for(unsigned ready=attempt;ready<120;++ready) {
                    if(start(&host)) {runtime->log("Tools add-on runtime started; UI initialization queued");return 1;}
                    Sleep(500);
                }
                break;
            }
            Sleep(500);
        }
        runtime->log("Timed out waiting for Asset Browser and supported Qt application");
        return 6;
    } catch(...) {if(runtime) runtime->log("Tools runtime initialization failed");return 7;}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) self=instance;
    return TRUE;
}
