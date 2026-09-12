#include "runtime.h"
#include "hammer_jobs.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
static void check(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
template<class T>static T symbol(HMODULE module,const char* name){auto result=reinterpret_cast<T>(GetProcAddress(module,name));check(result!=nullptr,name);return result;}
static uint64_t handle(ha::Runtime& runtime){
    std::smatch match;auto json=runtime.status_json();check(std::regex_search(json,match,std::regex("\"handle\":([0-9]+)")),"contribution handle");return std::stoull(match[1]);
}
static std::string call(ha::Runtime& runtime,uint64_t id,const char* phase="command",const char* control=""){
    char out[1024]{};check(runtime.invoke(id,"asset_browser",phase,control,"",out,sizeof(out))==HA_HANDLED,"invoke active contribution");return out;
}
static HMODULE active_module(const fs::path& root){
    for(const auto& file:fs::recursive_directory_iterator(root/"runtime-cache"))if(file.path().filename()=="reload_probe.dll"){
        auto module=GetModuleHandleW(fs::absolute(file.path()).c_str());
        if(module && symbol<int(HA_CALL*)()>(module,"ProbeActive")())return module;
    }
    throw std::runtime_error("active private DLL not found");
}
static fs::path binary;
static void probe(const fs::path& root,int version=1){
    const auto folder=root/"addons/reload_probe";fs::create_directories(folder);
    fs::copy_file(binary/(version==1?"reload_probe_v1.dll":"reload_probe_v2.dll"),folder/"reload_probe.dll",fs::copy_options::overwrite_existing);
    std::ofstream(folder/"addon.ini")<<"[addon]\nformat=1\nid=reload_probe\nversion=0.1.0\nabi=1\nentry=reload_probe.dll\nenabled=true\ntools=all\nreloadable=true\n";
}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2,"Pass checkout root");wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);binary=fs::path(exe).parent_path();
    const auto base=fs::path(argv[1])/"build/tests"/("reload-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directories(base);
    std::string message;
    {
        const auto root=base/"normal";fs::create_directories(root);
        ha::Runtime runtime(root,root/"settings");check(runtime.start().loaded==0,"empty startup");runtime.pump_jobs();
        probe(root);
        check(runtime.manage("load","",message),message);
        check(runtime.manage("load","",message) && message.find("Loaded 0")!=std::string::npos,"repeated scan does not duplicate");
        const auto old=active_module(root);const auto old_handle=handle(runtime);
        check(call(runtime,old_handle)=="version 1","first binary");
        int wrong=1;std::thread worker([&]{std::string reply;wrong=runtime.manage("reload","reload_probe",reply);});worker.join();check(!wrong,"wrong thread refused");
        auto mode=symbol<void(HA_CALL*)(int)>(old,"ProbeMode");
        mode(1);check(!runtime.manage("reload","reload_probe",message) && call(runtime,old_handle)=="version 1","veto preserves instance");mode(0);
        check(symbol<int(HA_CALL*)()>(old,"ProbePost")(),"queue callback");
        check(!runtime.manage("reload","reload_probe",message) && message.find("pending")!=std::string::npos,"pending delivery refuses reload");runtime.pump_jobs();
        const auto job=symbol<uint64_t(HA_CALL*)()>(old,"ProbeJob")();check(job!=0,"start worker");
        check(!runtime.manage("reload","reload_probe",message),"running worker refuses reload");
        check(symbol<int(HA_CALL*)(uint64_t)>(old,"ProbeCancel")(job)!=0,"cancel worker");
        // Drain terminal delivery, then inspect its effect through a successful reload below.
        for(int i=0;i<100;++i){runtime.pump_jobs();Sleep(2);}
        // A compiler still writing the replacement must not retire the current instance.
        HANDLE writing=CreateFileW((root/"addons/reload_probe/reload_probe.dll").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(writing!=INVALID_HANDLE_VALUE,"installed DLL is writable while its copy runs");
        check(!runtime.manage("reload","reload_probe",message) && call(runtime,old_handle)=="version 1","incomplete build refuses reload");
        CloseHandle(writing);
        probe(root,2); // Replace the installed DLL while version 1 is mapped.

        std::ofstream(root/"disabled")<<"";check(!runtime.manage("reload","reload_probe",message),"global disable respected");fs::remove(root/"disabled");
        fs::copy_file(binary/"hello.dll",root/"addons/reload_probe/extra.dll");
        check(!runtime.manage("reload","reload_probe",message) && message.find("one DLL")!=std::string::npos,"private DLL dependencies refused before retirement");
        fs::remove(root/"addons/reload_probe/extra.dll");
        check(runtime.manage("reload","reload_probe",message),message);
        check(call(runtime,handle(runtime))=="version 2" && handle(runtime)!=old_handle,"new code and new contribution handle");
        char out[32]{};check(runtime.invoke(old_handle,"asset_browser","command","","",out,sizeof(out))==HA_ERROR,"stale UI handle rejected");
        check(symbol<int(HA_CALL*)()>(old,"ProbeStops")()==1,"old shutdown once");
        check(!symbol<int(HA_CALL*)()>(old,"ProbePost")() && !symbol<int(HA_CALL*)()>(old,"ProbeWrite")(),"retired context cannot queue work or change settings");
        check(GetModuleHandleW(fs::absolute(root/"addons/reload_probe/reload_probe.dll").c_str())==nullptr,"original DLL never mapped");
        for(int i=0;i<30;++i)check(runtime.manage("reload","reload_probe",message),message);
        check(!runtime.manage("reload","reload_probe",message) && message.find("32 generations")!=std::string::npos,"generation limit");
        runtime.shutdown();check(!runtime.manage("load","",message),"management after shutdown refused");
    }
    for(int fault:{2,3,4}){
        const auto root=base/("fault"+std::to_string(fault));probe(root);
        ha::Runtime runtime(root,root/"settings");check(runtime.start().loaded==1,"fault fixture loaded");runtime.pump_jobs();
        symbol<void(HA_CALL*)(int)>(active_module(root),"ProbeMode")(fault);
        check(!runtime.manage("reload","reload_probe",message) && message.find("restart required")!=std::string::npos,"lifecycle exception requires restart");
        check(runtime.status_json().find("\"contributions\":[]")!=std::string::npos,"fault removes active contributions");runtime.shutdown();
    }
    {
        const auto root=base/"counter";const auto folder=root/"addons/reload_counter";fs::create_directories(folder);
        fs::copy_file(binary/"reload_counter.dll",folder/"reload_counter.dll");fs::copy_file(fs::path(argv[1])/"addons/reload_counter/addon.ini",folder/"addon.ini");
        ha::Runtime runtime(root,root/"settings");check(runtime.start().loaded==1,"counter example loads");runtime.pump_jobs();
        call(runtime,handle(runtime),"panel.click","increment");check(runtime.manage("reload","reload_counter",message),message);
        check(runtime.status_json().find("Count: 1")!=std::string::npos,"example preserves state across generations");runtime.shutdown();
    }
    {
        const auto root=base/"bad-replacement";probe(root);
        ha::Runtime runtime(root,root/"settings");check(runtime.start().loaded==1,"replacement fixture loads");runtime.pump_jobs();
        const auto old=active_module(root);
        fs::copy_file(binary/"hello.dll",root/"addons/reload_probe/reload_probe.dll",fs::copy_options::overwrite_existing);
        check(!runtime.manage("reload","reload_probe",message) && message.find("previous instance is stopped")!=std::string::npos,"bad replacement reports stopped state");
        check(symbol<int(HA_CALL*)()>(old,"ProbeStops")()==1,"retired even when replacement fails");
        check(runtime.status_json().find("\"contributions\":[]")!=std::string::npos,"failed replacement has no active UI");runtime.shutdown();
    }
    {
        const auto root=base/"ordinary";const auto folder=root/"addons/hello";fs::create_directories(folder);
        fs::copy_file(binary/"hello.dll",folder/"hello.dll");fs::copy_file(fs::path(argv[1])/"addons/hello/addon.ini",folder/"addon.ini");
        std::ofstream(folder/"addon.ini")<<"[addon]\nformat=1\nid=hello\nversion=0.1.0\nabi=1\nentry=hello.dll\nenabled=true\n";
        ha::Runtime runtime(root,root/"settings");check(runtime.start().loaded==1,"ordinary add-on loads");runtime.pump_jobs();
        check(!runtime.manage("reload","hello",message) && message.find("not opted")!=std::string::npos,"non-reloadable add-on refuses reload");runtime.shutdown();
    }

    // Exercise every shipped manifest/export pair, including subscriptions and picker scope.
    for(const auto* name:{"hello","commands","panel_settings","menu_hooks","note_import","picker_notes","editor_watch","live_status","compile_report","tool_console","project_context","steam_context"}){
        const auto root=base/(std::string("example-")+name);const auto folder=root/"addons"/name;fs::create_directories(folder);
        fs::copy_file(binary/(std::string(name)+".dll"),folder/(std::string(name)+".dll"));
        fs::copy_file(fs::path(argv[1])/"addons"/name/"addon.ini",folder/"addon.ini");
        ha::Runtime runtime(root,root/"settings",false,std::string(name)=="picker_notes"?"project_picker":"tools");
        check(runtime.start().loaded==1,std::string("example loads: ")+name);runtime.pump_jobs();
        for(int generation=0;generation<2;++generation){
            check(runtime.manage("reload",name,message),std::string(name)+": "+message);runtime.pump_jobs();
            check(runtime.status_json().find("Failed")==std::string::npos,"replacement remains healthy after dispatch");
        }
        runtime.shutdown();
    }
    for(int fault:{5,6,7}){
        const auto root=base/("callback-fault"+std::to_string(fault));probe(root);
        const auto healthy=root/"addons/commands";fs::create_directories(healthy);
        fs::copy_file(binary/"commands.dll",healthy/"commands.dll");fs::copy_file(fs::path(argv[1])/"addons/commands/addon.ini",healthy/"addon.ini");
        ha::Runtime runtime(root,root/"settings");check(runtime.start().loaded==2,"fault and healthy add-ons load together");runtime.pump_jobs();
        const auto module=active_module(root);
        const auto* host=symbol<const HA_HostV1*(HA_CALL*)()>(module,"ProbeHost")();
        const auto* ext=HA_GetExtensions(host);char text[32]{};
        check(!ext->get_setting(nullptr,"x",text,sizeof(text)) && !ext->set_setting(nullptr,"x","y") && !ext->register_contribution(nullptr,nullptr) && !ext->set_panel_text(nullptr,0,"x","y"),"null SDK contexts return errors");
        symbol<void(HA_CALL*)(int)>(module,"ProbeMode")(fault);
        const auto job=symbol<uint64_t(HA_CALL*)()>(module,"ProbeJob")();check(job!=0,"fault owner starts worker");
        for(int i=0;i<1000 && !symbol<int(HA_CALL*)()>(module,"ProbeStarted")();++i)Sleep(1);
        check(symbol<int(HA_CALL*)()>(module,"ProbeStarted")()!=0,"worker entered before inducing failure");
        check(symbol<int(HA_CALL*)()>(module,"ProbePost")()!=0,"fault owner queues delivery");
        if(fault==5){
            // Locate the probe command, which is registered after the healthy command.
            const auto json=runtime.status_json();std::regex pattern("\"handle\":([0-9]+)");uint64_t id=0;
            for(std::sregex_iterator it(json.begin(),json.end(),pattern),end;it!=end;++it)id=std::stoull((*it)[1]);
            char response[32]{};check(runtime.invoke(id,"asset_browser","command","","",response,sizeof(response))==HA_ERROR && response[0]==0,"throwing full-buffer callback returns an empty safe response");
        }else if(fault==6)runtime.event("failure","test");
        else runtime.pump_jobs();
        // No pump: cancellation must happen synchronously with the failed callback.
        for(int i=0;i<1000 && !symbol<int(HA_CALL*)()>(module,"ProbeCancelled")();++i)Sleep(1);
        check(symbol<int(HA_CALL*)()>(module,"ProbeCancelled")()!=0,"failure immediately cancels owned worker");
        check(!ext->set_setting(host->context,"stale","write") && !ext->get_setting(host->context,"stale",text,sizeof(text)),"failed context cannot access settings");
        check(!symbol<int(HA_CALL*)()>(module,"ProbePost")(),"failed context cannot enqueue callbacks");
        const auto delivered=symbol<int(HA_CALL*)()>(module,"ProbeDeliveries")();runtime.pump_jobs();
        check(symbol<int(HA_CALL*)()>(module,"ProbeDeliveries")()==delivered,"no further dispatch to failed owner");
        check(runtime.manage("reload","commands",message),"healthy sibling still reloads after failure");
        runtime.shutdown();check(symbol<int(HA_CALL*)()>(module,"ProbeStops")()==0,"broken owner shutdown is not called");
    }
    {
        const auto root=base/"partial-load";probe(root);
        fs::copy_file(binary/"reload_probe_v3.dll",root/"addons/reload_probe/reload_probe.dll",fs::copy_options::overwrite_existing);
        ha::Runtime runtime(root,root/"settings");check(runtime.start().rejected==1,"partial initialization failure is contained");
        const auto* host=symbol<const HA_HostV1*(HA_CALL*)()>(active_module(root),"ProbeHost")();
        check(!HA_GetExtensions(host)->set_setting(host->context,"after_failure","bad"),"partially initialized host is revoked");
        check(runtime.status_json().find("\"contributions\":[]")!=std::string::npos,"partial contributions never exposed");runtime.shutdown();
    }
    std::cout<<"Hot reload tests passed: DLL replacement, veto, jobs, stale callbacks, lifecycle faults, all examples, immediate cancellation, response buffers, generation limit and persisted state.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
