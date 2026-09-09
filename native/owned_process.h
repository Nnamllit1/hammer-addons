#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace ha {
std::wstring quote_argument(const std::wstring& value);
// Owns only a process created by this object. There is no attach-by-PID API.
class ToolsProcess {
    HANDLE process_=nullptr, main_=nullptr, job_=nullptr;
    DWORD pid_=0;
    bool released_=false;
public:
    ToolsProcess(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments);
    ~ToolsProcess();
    ToolsProcess(const ToolsProcess&)=delete;
    ToolsProcess& operator=(const ToolsProcess&)=delete;
    void start_runtime(const std::filesystem::path& dll);
    void release();
    DWORD pid() const { return pid_; }
    HANDLE handle() const { return process_; }
};
}
