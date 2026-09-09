#include "owned_process.h"
#include <tlhelp32.h>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <future>
namespace fs=std::filesystem;
namespace {
struct Handle {
    HANDLE value;
    ~Handle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
[[noreturn]] void error(const char* text) { throw std::runtime_error(std::string(text)+" (Windows "+std::to_string(GetLastError())+")"); }
uintptr_t module_base(DWORD pid,const fs::path& path) {
    // A snapshot can race the loader while it updates its module list.
    for(int attempt=0;attempt<250;++attempt) {
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid)};
        if(snapshot.value==INVALID_HANDLE_VALUE) {
            if(GetLastError()==ERROR_BAD_LENGTH || GetLastError()==ERROR_PARTIAL_COPY) { Sleep(20); continue; }
            error("Cannot inspect the tools process");
        }
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(Module32FirstW(snapshot.value,&entry)) do {
            if(_wcsicmp(entry.szExePath,path.c_str())==0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        } while(Module32NextW(snapshot.value,&entry));
        Sleep(20);
    }
    throw std::runtime_error("Expected module was not loaded in the tools process");
}
uintptr_t remote_system_function(DWORD pid,const char* name) {
    auto function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);
    HMODULE owner=nullptr;
    if(!function || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(function),&owner)) error("Cannot resolve Windows loader");
    wchar_t file[32768]{};
    if(!GetModuleFileNameW(owner,file,32768)) error("Cannot locate Windows loader");
    // Resolve the owning module (including forwarded exports), not a guessed remote address.
    return module_base(pid,file)+(reinterpret_cast<uintptr_t>(function)-reinterpret_cast<uintptr_t>(owner));
}
DWORD call(HANDLE process,uintptr_t function,void* parameter,DWORD timeout) {
    Handle thread{CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),parameter,0,nullptr)};
    if(!thread.value) error("Windows refused to start the add-on loader");
    const auto wait=WaitForSingleObject(thread.value,timeout);
    if(wait!=WAIT_OBJECT_0) throw std::runtime_error("Add-on startup timed out or the tools process stopped responding");
    DWORD result=0;
    if(!GetExitCodeThread(thread.value,&result)) error("Cannot read add-on startup result");
    return result;
}
uintptr_t start_rva(const fs::path& file) {
    // Map as data: inspecting exports must not run this DLL in the launcher.
    HMODULE image=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!image) error("Cannot inspect add-on runtime");
    struct Cleanup { HMODULE h;~Cleanup(){FreeLibrary(h);} } cleanup{image};
    const auto base=reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(image)&~uintptr_t(3));
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error("Runtime must be a Windows x64 DLL");
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!dir.VirtualAddress) throw std::runtime_error("Runtime export table missing");
    const auto table=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+dir.VirtualAddress);
    const auto names=reinterpret_cast<const DWORD*>(base+table->AddressOfNames);
    const auto ordinals=reinterpret_cast<const WORD*>(base+table->AddressOfNameOrdinals);
    const auto functions=reinterpret_cast<const DWORD*>(base+table->AddressOfFunctions);
    for(DWORD i=0;i<table->NumberOfNames;++i) if(strcmp(reinterpret_cast<const char*>(base+names[i]),"HA_StartTools")==0) {
        const auto address=functions[ordinals[i]];
        if(address>=dir.VirtualAddress && address<dir.VirtualAddress+dir.Size) break;
        return address;
    }
    throw std::runtime_error("Runtime is missing HA_StartTools");
}
}
namespace ha {
std::wstring quote_argument(const std::wstring& value) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t ch:value) {
        if(ch==L'\\') { ++slashes; continue; }
        if(ch==L'\"') out.append(slashes*2+1,L'\\');
        else out.append(slashes,L'\\');
        slashes=0;out+=ch;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
ToolsProcess::ToolsProcess(const fs::path& executable,const std::vector<std::wstring>& arguments,bool project_picker) : picker_(project_picker) {
    std::wstring command=quote_argument(executable.wstring());
    for(const auto& value:arguments) command+=L" "+quote_argument(value);
    job_=CreateJobObjectW(nullptr,nullptr);
    if(!job_) error("Cannot create tools startup job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {CloseHandle(job_);job_=nullptr;error("Cannot protect tools startup");}
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|(picker_ ? DEBUG_PROCESS : 0),nullptr,
        executable.parent_path().c_str(),&startup,&info)) {CloseHandle(job_);job_=nullptr;error("Cannot start CS2 Workshop Tools");}
    process_=info.hProcess;main_=info.hThread;pid_=info.dwProcessId;
    if(!AssignProcessToJobObject(job_,process_)) {
        TerminateProcess(process_,1);WaitForSingleObject(process_,5000);
        CloseHandle(main_);CloseHandle(process_);CloseHandle(job_);main_=process_=job_=nullptr;
        error("Cannot protect the new tools process");
    }
}
ToolsProcess::~ToolsProcess() {
    if(!released_ && process_) {TerminateProcess(process_,1);WaitForSingleObject(process_,5000);}
    if(main_) CloseHandle(main_);
    if(process_) CloseHandle(process_);
    if(job_) CloseHandle(job_);
}
bool ToolsProcess::wait_for_tools(const fs::path& executable,const fs::path& picker_runtime) {
    if(!picker_) throw std::runtime_error("This process is not a Workshop project picker");
    if(ResumeThread(main_)==DWORD(-1)) error("Cannot open Workshop project picker");
    resumed_=true;
    std::future<void> initialization;
    if(!picker_runtime.empty()) initialization=std::async(std::launch::async,[this,picker_runtime]{start_runtime(picker_runtime);});
    // Always terminate on exceptional exit before the future waits for a remote call;
    // otherwise that call could be stuck behind an unhandled debug event.
    struct StartupGuard { HANDLE process;bool complete=false;~StartupGuard(){if(!complete)TerminateProcess(process,1);} } guard{process_};
    // Debug creation events identify our own descendants before they run. We do not
    // scan for, attach to, or modify an independently started CS2 session.
    bool initialBreakpoint=false;
    for(;;) {
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,100)) {
            if(GetLastError()==ERROR_SEM_TIMEOUT) {
                if(initialization.valid() && initialization.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)
                    initialization.get();
                continue;
            }
            error("Cannot wait for Workshop project selection");
        }
        DWORD disposition=DBG_CONTINUE;
        bool selected=false,otherChild=false;
        Handle childProcess{nullptr},childThread{nullptr};
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
            const auto& info=event.u.CreateProcessInfo;
            if(info.hFile) CloseHandle(info.hFile);
            if(event.dwProcessId!=pid_) {
                wchar_t path[32768]{};DWORD length=32768;BOOL owned=FALSE;
                if(!QueryFullProcessImageNameW(info.hProcess,0,path,&length) ||
                    !IsProcessInJob(info.hProcess,job_,&owned) || !owned)
                    throw std::runtime_error("Cannot verify the project picker's child process");
                selected=_wcsicmp(fs::path(path).lexically_normal().make_preferred().c_str(),
                    fs::absolute(executable).lexically_normal().make_preferred().c_str())==0;
                otherChild=!selected;
                if(selected) {
                    // Keep our own handles: debug-event handles belong to the debug session.
                    if(!DuplicateHandle(GetCurrentProcess(),info.hProcess,GetCurrentProcess(),&childProcess.value,0,FALSE,DUPLICATE_SAME_ACCESS) ||
                       !DuplicateHandle(GetCurrentProcess(),info.hThread,GetCurrentProcess(),&childThread.value,0,FALSE,DUPLICATE_SAME_ACCESS))
                        error("Cannot retain the selected tools process");
                    if(SuspendThread(childThread.value)==DWORD(-1)) error("Cannot prepare the selected tools process");
                }
            }
        } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
            if(event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
        } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
            // Handle Windows' initial debugger breakpoint; let the application handle other exceptions.
            if(event.dwProcessId==pid_ && !initialBreakpoint && event.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT)
                initialBreakpoint=true;
            else disposition=DBG_EXCEPTION_NOT_HANDLED;
        }
        const bool cancelled=event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT && event.dwProcessId==pid_;
        if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,disposition)) error("Cannot continue Workshop project picker");
        if(otherChild && !DebugActiveProcessStop(event.dwProcessId)) error("Cannot release picker helper process");
        if(cancelled) { guard.complete=true; if(initialization.valid()) {try{initialization.get();}catch(...){}} return false; }
        if(selected) {
            // Detach before runtime initialization so normal DLL loading cannot block
            // waiting for this thread to service another debug event.
            if(!DebugActiveProcessStop(event.dwProcessId)) error("Cannot release selected tools from startup tracking");
            if(!DebugActiveProcessStop(pid_) && WaitForSingleObject(process_,0)!=WAIT_OBJECT_0)
                error("Cannot release Workshop project picker");
            // Runtime work now proceeds without debug-event pumping. If the user
            // selected a project immediately, allow that picker process to finish.
            if(initialization.valid()) {try{initialization.get();}catch(...){
                if(WaitForSingleObject(process_,0)!=WAIT_OBJECT_0)throw;
            }}
            guard.complete=true;
            CloseHandle(main_);CloseHandle(process_);
            main_=childThread.value;childThread.value=nullptr;
            process_=childProcess.value;childProcess.value=nullptr;
            pid_=event.dwProcessId;picker_=false;resumed_=false;
            return true;
        }
    }
}
void ToolsProcess::start_runtime(const fs::path& file) {
    const auto path=fs::absolute(file);
    const auto rva=start_rva(path);
    if(!resumed_ && ResumeThread(main_)==DWORD(-1)) error("Cannot resume tools startup");
    const auto bytes=(path.wstring().size()+1)*sizeof(wchar_t);
    auto* remote=VirtualAllocEx(process_,nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!remote) error("Cannot allocate runtime path");
    // On failure the owned process is terminated before its path allocation is reclaimed.
    SIZE_T written=0;
    if(!WriteProcessMemory(process_,remote,path.c_str(),bytes,&written) || written!=bytes) error("Cannot pass runtime path");
    call(process_,remote_system_function(pid_,"LoadLibraryW"),remote,15000);
    VirtualFreeEx(process_,remote,0,MEM_RELEASE);
    // Do not use GetExitCodeThread as a 64-bit HMODULE; it truncates pointers.
    const auto base=module_base(pid_,path);
    const auto result=call(process_,base+rva,nullptr,75000);
    if(result!=1) throw std::runtime_error("Tools runtime rejected startup (code "+std::to_string(result)+"). See loader.log beside the runtime.");
    if(WaitForSingleObject(process_,0)!=WAIT_TIMEOUT) throw std::runtime_error("The tools process exited during startup");
}
void ToolsProcess::release() {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if(!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) error("Cannot finish tools startup");
    released_=true;
}
}
