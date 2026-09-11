#include "tool_logs.h"
#include <filesystem>
#include <iostream>
#include <cstring>
#include <thread>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv) {
    try {
        ha::ToolLogs logs;
        check(!logs.attach(nullptr) && !logs.attach(GetModuleHandleW(nullptr)),"unsupported module rejected");
        if(argc==1){std::cout<<"Tool logs unavailable-provider test passed. Pass original tier0.dll for the optional real-engine test.\n";return 0;}
        check(argc==2,"pass original tier0.dll path");
        const auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        check(module!=nullptr,"load original tier0");
        check(logs.attach(module) && logs.available(),"verified engine listener attached");
        auto find=reinterpret_cast<int(__cdecl*)(const char*)>(GetProcAddress(module,"LoggingSystem_FindChannel"));
        auto first=reinterpret_cast<int(__cdecl*)()>(GetProcAddress(module,"LoggingSystem_GetFirstChannelID"));
        auto emit=reinterpret_cast<int(__cdecl*)(int,int,const char*)>(GetProcAddress(module,"LoggingSystem_LogDirect"));
        check(find && first && emit,"logging exports");
        int channel=find("General");if(channel<0)channel=first();check(channel>=0,"existing logging channel");
        emit(channel,HA_LOG_MESSAGE,"Hammer Addons listener validation\n");
        auto lines=logs.take();bool found=false;
        for(const auto& line:lines)if(std::strstr(line.text.data(),"Hammer Addons listener validation")) {
            found=true;check(line.channel==channel && line.severity==HA_LOG_MESSAGE && !line.truncated,"context ABI validated");
        }
        check(found,"original tier0 delivered marker to listener");
        std::thread worker([&]{emit(channel,HA_LOG_WARNING,"Hammer Addons worker logging validation\n");});worker.join();
        found=false;
        for(const auto& line:logs.take())if(std::strstr(line.text.data(),"worker logging validation")) {
            found=true;check(line.severity==HA_LOG_WARNING,"worker severity");
        }
        check(found,"cross-thread engine log captured");
        for(int i=0;i<300;++i)emit(channel,HA_LOG_MESSAGE,"Bounded capture fixture\n");
        size_t captured=0;uint64_t dropped=0,sequence=0;
        for(auto batch=logs.take();!batch.empty();batch=logs.take())for(const auto& line:batch) {
            check(line.sequence>sequence,"capture sequence increases");sequence=line.sequence;
            dropped=line.dropped;++captured;
        }
        check(captured==256 && dropped>=44,"bounded capture reports overflow");
        std::cout<<"Original tier0 listener passed: verified DLL hash, message/context ABI, worker-thread logging and bounded overflow reporting.\n";
        // Listener/module stay alive until process exit, matching runtime ownership.
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
