#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ha {
bool plain_path(const std::filesystem::path&);
namespace snapshot_detail {
namespace fs=std::filesystem;
struct Locks {
    std::vector<HANDLE> files;
    ~Locks(){for(const auto file:files)CloseHandle(file);}
    HANDLE hold(const fs::path& path,bool directory=false){
        const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|(directory?FILE_SHARE_WRITE:0),nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock reload package files; finish copying or building, then retry");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
           bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory){
            CloseHandle(file);throw std::runtime_error("Invalid reload package path");
        }
        try{files.push_back(file);}catch(...){CloseHandle(file);throw;}
        return file;
    }
    void ancestors(const fs::path& directory){
        fs::path cursor;
        for(const auto& part:fs::absolute(directory)){cursor/=part;if(cursor.has_relative_path())hold(cursor,true);}
    }
};
inline void rewind(HANDLE file){
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot rewind snapshot file");
}
inline DWORD read(HANDLE file,std::array<char,65536>& data){
    DWORD size=0;if(!ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&size,nullptr))throw std::runtime_error("Cannot read snapshot file");return size;
}
using Inventory=std::map<fs::path,bool>;
inline Inventory inventory(const fs::path& source){
    Inventory result;
    for(const auto& file:fs::recursive_directory_iterator(source)){
        if(result.size()>=1024)throw std::runtime_error("Reload package exceeds 1024 entries");
        if(!plain_path(file.path()))throw std::runtime_error("Reload package contains a reparse point");
        const bool directory=file.is_directory();
        if(!directory && !file.is_regular_file())throw std::runtime_error("Reload package contains a non-regular file");
        result.emplace(file.path().lexically_relative(source),directory);
    }
    return result;
}
struct State {
    fs::path cache,generation;
    std::vector<fs::path> created_files,created_directories;
    Locks locks;
    bool retained=false;
    ~State(){
        const auto release=[&]{for(auto file:locks.files)CloseHandle(file);locks.files.clear();};
        if(retained || generation.empty()){release();return;}
        try{
            if(!generation.is_absolute() || generation.parent_path()!=cache || !plain_path(generation)){release();return;}
            // Acquire overlapping directory locks before releasing file locks.
            // Cleanup must never traverse parents that can be renamed/replaced.
            Locks cleanup;cleanup.ancestors(generation);
            std::vector<fs::path> directories;
            for(const auto& directory:created_directories)if(fs::exists(directory)){
                cleanup.hold(directory,true);directories.push_back(directory);
            }
            release();
            const auto remove_owned=[&](const fs::path& path){
                const auto relative=path.lexically_relative(generation);
                if(relative.empty() || relative.is_absolute() || *relative.begin()==".." || !plain_path(path))return;
                std::error_code error;fs::remove(path,error);
                if(error)OutputDebugStringA("Hammer Addons: could not remove an unused reload-cache entry.\n");
            };
            for(auto it=created_files.rbegin();it!=created_files.rend();++it)remove_owned(*it);
            for(auto it=directories.rbegin();it!=directories.rend();++it){
                // Release the child's own lock; its ancestors remain pinned.
                CloseHandle(cleanup.files.back());cleanup.files.pop_back();remove_owned(*it);
            }
            CloseHandle(cleanup.files.back());cleanup.files.pop_back();remove_owned(generation);
        }catch(...){release();}
    }
};
}
struct ReloadCopy {
    std::filesystem::path directory;
    std::shared_ptr<snapshot_detail::State> state;
    void retain() const noexcept {if(state)state->retained=true;}
};
// Internal deterministic test seam; runtime callers never supply an observer.
using SnapshotObserver=std::function<void(const char*,const std::filesystem::path&)>;
inline ReloadCopy stage_reloadable(const std::filesystem::path& input,const std::filesystem::path& root,
                                  const std::string& entry,const SnapshotObserver& observe={}){
    namespace fs=std::filesystem;
    using namespace snapshot_detail;
    const auto source=fs::absolute(input).lexically_normal();
    ReloadCopy result{{},std::make_shared<State>()};auto& state=*result.state;
    state.locks.ancestors(source);
    const auto expected=inventory(source);
    if(observe)observe("enumerated",source);
    std::map<fs::path,HANDLE> files;
    uint64_t total=0;
    for(const auto& [relative,directory]:expected){
        const auto file=state.locks.hold(source/relative,directory);
        if(directory)continue;
        auto extension=relative.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
        if(extension==L".dll" && relative!=fs::path(entry))throw std::runtime_error("Reloadable packages currently support one DLL; private DLL dependencies require restart");
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(file,&size) || size.QuadPart<0)throw std::runtime_error("Cannot size snapshot file");
        total+=static_cast<uint64_t>(size.QuadPart);
        if(total>1024ull*1024*1024)throw std::runtime_error("Reload package exceeds 1 GiB");
        files.emplace(relative,file);
    }
    if(!files.contains(fs::path(entry)))throw std::runtime_error("Reload package DLL is missing");
    // Directory handles protect names against replacement, not child creation.
    // Compare membership again only after every known file has a write-denying lock.
    if(inventory(source)!=expected)throw std::runtime_error("Reload package entries changed during snapshot; retry");
    state.cache=fs::absolute(root/"runtime-cache").lexically_normal();
    if(!plain_path(state.cache))throw std::runtime_error("Invalid reload cache path");
    fs::create_directories(state.cache);state.locks.ancestors(state.cache);
    static std::atomic<uint64_t> serial{0};
    for(;;){
        auto candidate=state.cache/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++serial));
        if(fs::create_directory(candidate)){state.generation=std::move(candidate);break;}
    }
    state.created_files.reserve(files.size());state.created_directories.reserve(expected.size()+1);
    result.directory=state.generation/source.filename();
    state.created_directories.push_back(result.directory);fs::create_directory(result.directory);
    state.locks.hold(state.generation,true);state.locks.hold(result.directory,true);
    for(const auto& [relative,directory]:expected)if(directory){
        const auto target=result.directory/relative;state.created_directories.push_back(target);fs::create_directory(target);state.locks.hold(target,true);
    }
    for(const auto& [relative,file]:files){
        const auto target=result.directory/relative;
        // Read the frozen source handle, never reopen its path while copying.
        const auto output=CreateFileW(target.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create reload snapshot file");
        struct Close{HANDLE file;~Close(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}} close{output};
        state.created_files.push_back(target);
        std::array<char,65536> data{};
        for(DWORD size=read(file,data);size;size=read(file,data)){
            DWORD written=0;if(!WriteFile(output,data.data(),size,&written,nullptr) || written!=size)throw std::runtime_error("Cannot write reload snapshot file");
        }
        CloseHandle(output);close.file=INVALID_HANDLE_VALUE;
        if(observe)observe("copied",target);
        const auto copy=state.locks.hold(target);
        // Close the write-to-read handoff race by verifying bytes after acquiring
        // the destination lock. Keep that lock until native loading completes.
        rewind(file);rewind(copy);std::array<char,65536> other{};
        for(;;){const auto left=read(file,data),right=read(copy,other);
            if(left!=right || !std::equal(data.begin(),data.begin()+left,other.begin()))throw std::runtime_error("Reload snapshot bytes changed; retry");
            if(!left)break;
        }
    }
    if(observe)observe("copied_all",result.directory);
    if(inventory(source)!=expected || inventory(result.directory)!=expected)throw std::runtime_error("Reload package entries changed during snapshot; retry");
    return result;
}
}
