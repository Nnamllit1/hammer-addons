#include "reload_snapshot.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static void write(const fs::path& path,const char* value){std::ofstream out(path,std::ios::binary);out<<value;check(out.good(),"write fixture");}
static size_t generations(const fs::path& root){return fs::exists(root/"runtime-cache")?static_cast<size_t>(std::distance(fs::directory_iterator(root/"runtime-cache"),fs::directory_iterator{})):0;}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2,"Pass checkout root");
    const auto root=fs::path(argv[1])/"build/tests"/("snapshot-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    const auto source=root/"addons/sample";fs::create_directories(source/"data");
    write(source/"sample.dll","DLL bytes");write(source/"addon.ini","manifest bytes");write(source/"data/value.txt","data bytes");
    fs::create_directories(source/"data/nested/empty");
    std::string payload(2*65536+137,'\0');
    for(size_t i=0;i<payload.size();++i)payload[i]=static_cast<char>(i%251);
    {std::ofstream out(source/"data/nested/binary.dat",std::ios::binary);out.write(payload.data(),payload.size());check(out.good(),"write multi-buffer fixture");}
    const auto fail=[&](const ha::SnapshotObserver& observer){
        bool rejected=false;try{auto copy=ha::stage_reloadable(source,root,"sample.dll",observer);}catch(const std::exception&){rejected=true;}
        check(rejected,"mutation must reject snapshot");check(generations(root)==0,"failed attempt must not abandon a generation");
    };
    fail([&](const char* phase,const fs::path&){if(std::string(phase)=="enumerated")write(source/"late.txt","new member");});
    fs::remove(source/"late.txt");
    fail([&](const char* phase,const fs::path&){if(std::string(phase)=="enumerated")fs::remove(source/"data/value.txt");});
    write(source/"data/value.txt","data bytes");
    fail([&](const char* phase,const fs::path& target){if(std::string(phase)=="copied" && target.filename()=="sample.dll")write(target,"tampered copy");});
    fail([&](const char* phase,const fs::path& target){if(std::string(phase)=="copied" && target.filename()=="sample.dll")write(target,"DLL bytes with extra bytes");});
    fail([&](const char* phase,const fs::path& target){if(std::string(phase)=="copied" && target.filename()=="sample.dll")write(target,"DLL");});
    fail([&](const char* phase,const fs::path&){if(std::string(phase)=="copied_all")write(source/"late.txt","added during copying");});
    fs::remove(source/"late.txt");
    // A partial copy exception cleans up the files created before it failed.
    fail([&](const char* phase,const fs::path&){if(std::string(phase)=="copied")throw std::runtime_error("copy interrupted");});
    fs::path owned_generation;
    {
        auto copy=ha::stage_reloadable(source,root,"sample.dll");owned_generation=copy.directory.parent_path();
        check(generations(root)==1,"live staged generation exists");
        {std::ifstream in(copy.directory/"data/nested/binary.dat",std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>{});
            check(bytes==payload,"multi-buffer binary copy preserves every byte including trailing partial buffer");}
        check(fs::is_directory(copy.directory/"data/nested/empty"),"empty nested directory preserved");
        for(const auto& path:{source/"sample.dll",copy.directory/"sample.dll"}){
            const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            check(handle==INVALID_HANDLE_VALUE,"source and destination remain write locked");
        }
        check(!MoveFileW(copy.directory.c_str(),(copy.directory.parent_path()/"moved").c_str()),"snapshot directory cannot be renamed under held locks");
        auto second_owner=copy;copy={};check(fs::exists(second_owner.directory),"shared snapshot keeps locks and generation alive");
    }
    check(!fs::exists(owned_generation) && generations(root)==0,"unconsumed copy removed when last owner releases it");
    {
        auto copy=ha::stage_reloadable(source,root,"sample.dll");owned_generation=copy.directory.parent_path();copy.retain();
    }
    check(fs::exists(owned_generation),"native load attempt retains generation");
    const auto retained_generation=owned_generation;
    const auto unrelated=root/"runtime-cache/unrelated";fs::create_directory(unrelated);write(unrelated/"keep.txt","unrelated data");
    {
        auto copy=ha::stage_reloadable(source,root,"sample.dll");owned_generation=copy.directory.parent_path();
    }
    check(!fs::exists(owned_generation),"unused snapshot removed beside existing generations");
    check(fs::exists(retained_generation/"sample/sample.dll") && fs::exists(unrelated/"keep.txt"),"cleanup preserves retained and unrelated generations");
    fs::path unexpected;
    {
        auto copy=ha::stage_reloadable(source,root,"sample.dll");owned_generation=copy.directory.parent_path();
        unexpected=copy.directory/"not-owned.txt";write(unexpected,"external file");
    }
    check(fs::exists(unexpected) && !fs::exists(owned_generation/"sample/sample.dll"),"cleanup removes owned files but never sweeps unexpected files");
    HANDLE reader=INVALID_HANDLE_VALUE;fs::path blocked;
    {
        auto copy=ha::stage_reloadable(source,root,"sample.dll");blocked=copy.directory/"sample.dll";
        reader=CreateFileW(blocked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(reader!=INVALID_HANDLE_VALUE,"external reader blocks deletion");
    }
    check(fs::exists(blocked) && !fs::exists(blocked.parent_path()/"addon.ini"),"blocked deletion is best effort and cleanup continues for other files");
    CloseHandle(reader);
    check(fs::remove(blocked),"snapshot released its locks even when cleanup was blocked");
    check(fs::exists(source/"sample.dll") && fs::exists(source/"data/value.txt"),"cleanup never deletes installed package");
    std::cout<<"Snapshot tests passed: membership races, copy tampering, interrupted copies, handle lifetime, cleanup and retained generations.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
