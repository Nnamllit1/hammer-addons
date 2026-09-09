#include "owned_process.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    try {
        const fs::path executable=argv[1],runtime=argv[2];
        const std::wstring mode=argv[3];
        const bool picker=mode==L"picker-cancel" || mode==L"picker-ui" || mode==L"picker" || mode==L"picker-reject" || mode==L"cancel";
        const bool reject=mode==L"reject" || mode==L"picker-reject";
        // Production's CLI always supplies both flags. Verify the runtime independently rejects a normal session.
        std::vector<std::wstring> args{L"-tools"};
        if(!reject)args.push_back(L"-insecure");
        if(picker)args={mode,L"-insecure"};
        ha::ToolsProcess process(executable,args,picker);
        if(picker) {
            const bool selected=process.wait_for_tools(executable.parent_path()/L"cs2.exe",(mode==L"picker-ui" || mode==L"picker-cancel") ? runtime : fs::path{});
            if(mode==L"cancel" || mode==L"picker-cancel") {
                if(selected)throw std::runtime_error("Cancelled picker started tools");
                std::cout<<"Picker cancellation passed.\n";return 0;
            }
            if(!selected)throw std::runtime_error("Picker did not start tools");
        }
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
