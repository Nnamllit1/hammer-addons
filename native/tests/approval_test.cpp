#include "runtime.h"
#include "signing.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2,"Pass checkout root");wchar_t binary[32768]{};GetModuleFileNameW(nullptr,binary,32768);
    const auto root=fs::path(argv[1])/"build/tests"/("approval-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    const auto addon=root/"addons/hello";fs::create_directories(addon);
    fs::copy_file(fs::path(binary).parent_path()/"hello.dll",addon/"hello.dll");fs::copy_file(fs::path(argv[1])/"addons/hello/addon.ini",addon/"addon.ini");
    std::ofstream(addon/"data.txt")<<"original";
    int decisions=0;
    ha::review_addon_packages(root,[&](const ha::ApprovalRequest& request){
        ++decisions;check(request.id=="hello" && request.digest.size()==64,"review identifies exact package");
        HANDLE file=CreateFileW((addon/"data.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(file==INVALID_HANDLE_VALUE,"reviewed bytes are locked throughout decision");return false;
    });
    check(decisions==1 && !fs::exists(root/"local-approvals/hello.approval"),"decline creates no approval");
    {ha::Runtime runtime(root);check(runtime.start().rejected==1,"unapproved DLL rejected");}
    check(!GetModuleHandleW((addon/"hello.dll").c_str()),"unapproved DLL never mapped");
    ha::review_addon_packages(root,[&](const ha::ApprovalRequest&){++decisions;return true;});
    check(decisions==2,"first approval requested");
    ha::review_addon_packages(root,[&](const ha::ApprovalRequest&){++decisions;return true;});
    check(decisions==2,"unchanged approval reused without asking");
    {ha::Runtime runtime(root);check(runtime.start().loaded==1,"locally approved package loads");runtime.shutdown();}
    std::ofstream(addon/"data.txt")<<"modified";
    ha::review_addon_packages(root,[&](const ha::ApprovalRequest&){++decisions;return true;});
    check(decisions==2,"changed package is blocked without automatic reapproval");
    {ha::Runtime runtime(root);check(runtime.start().rejected==1,"changed package rejected");}
    ha::signing::approve_local(addon,root/"local-approvals");
    {ha::Runtime runtime(root);check(runtime.start().loaded==1,"explicit reapproval admits new snapshot");runtime.shutdown();}
    std::ofstream(root/"local-approvals/hello.approval")<<"tampered";
    ha::review_addon_packages(root,[&](const ha::ApprovalRequest&){++decisions;return true;});
    check(decisions==2,"corrupt approval does not silently grant or re-request trust");
    {ha::Runtime runtime(root);check(runtime.start().rejected==1,"corrupt receipt rejected");}
    std::cout<<"Approval tests passed: decline, first consent, locked snapshot, no DllMain before approval, repeat launch, changes, explicit reapproval and corrupt receipt.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
