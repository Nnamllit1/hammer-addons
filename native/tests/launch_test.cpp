#include "owned_process.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    try {
        const fs::path executable=argv[1],runtime=argv[2];
        const bool reject=std::wstring(argv[3])==L"reject";
        // Production's CLI always supplies both flags. Verify the runtime independently rejects a normal session.
        std::vector<std::wstring> args{L"-tools"};
        if(!reject)args.push_back(L"-insecure");
        ha::ToolsProcess process(executable,args);
        bool failed=false;
        try{process.start_runtime(runtime);}catch(const std::exception& e){std::cout<<e.what()<<'\n';failed=true;}
        if(reject) {if(!failed)throw std::runtime_error("Normal session was accepted");return 0;}
        if(failed)throw std::runtime_error("Tools session failed");
        process.release();
        if(WaitForSingleObject(process.handle(),15000)!=WAIT_OBJECT_0)throw std::runtime_error("Fixture did not see the add-on UI");
        DWORD result=0;GetExitCodeProcess(process.handle(),&result);
        if(result)throw std::runtime_error("Fixture UI check failed");
        std::cout<<"Own-process runtime loading and add-on UI passed.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
