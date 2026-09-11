#pragma once
#include "hammer_addons.h"
#include "extensions.h"
#include "hammer_editor.h"
#include "hammer_build.h"
#include "jobs.h"
#include "tool_logs.h"
#include "project.h"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ha {
struct Summary { unsigned loaded = 0, rejected = 0, disabled = 0; };
class Runtime {
public:
    explicit Runtime(std::filesystem::path root, std::filesystem::path settings_root = {}, bool factory_events = true, std::string process_scope = {});
    Summary start();
    bool initialize_project(const std::filesystem::path&,const std::vector<std::wstring>&);
    void event(const char* name, const char* value);
    void shutdown();
    void pump_jobs() noexcept;
    bool attach_tool_logs(HMODULE module);
    void editor_window(uint64_t id, bool open) noexcept;
    std::string status_json();
    int invoke(uint64_t handle, const char* tool, const char* phase, const char* control,
               const char* value, char* response, size_t capacity, const HA_EditorStateV1* editor = nullptr, const HA_BuildOutputV1* build = nullptr);
    void report_binding(uint64_t handle, const char* tool, const char* message);
    void initialization_error(const std::string& error);
    void log(std::string_view message) noexcept;
private:
    struct Status { std::string id, version, state, detail; std::vector<std::string> tools; };
    std::vector<Status> statuses_;
    std::string notice_;
    Settings settings_;
    Jobs jobs_;
    ToolLogs tool_logs_;
    Project project_;
    uint64_t next_subscription_ = 1;
    DWORD editor_thread_ = 0;
    uint64_t next_handle_ = 1;
    bool dispatching_ = false;
    struct Addon {
        Runtime* owner;
        std::string id, directory;
        HMODULE module = nullptr;
        const HA_AddonV1* api = nullptr;
        HA_HostV1 host{};
        bool active = false;
        bool registering = false;
        std::vector<Contribution> contributions;
        struct LogSubscription {uint64_t id;HA_LogCallbackFn callback;uint32_t minimum;uint64_t after;};
        std::vector<LogSubscription> logs;
        size_t status_index = 0;
    };
    static uint64_t HA_CALL register_contribution(void*, const HA_ContributionV1*);
    static size_t HA_CALL get_setting(void*, const char*, char*, size_t);
    static int HA_CALL set_setting(void*, const char*, const char*);
    static int HA_CALL set_panel_text(void*,uint64_t,const char*,const char*);
    static const HA_ProjectInfoV1* HA_CALL current_project(void*) noexcept;
    static size_t HA_CALL source_path(void*,const char*,char*,size_t) noexcept;
    static int HA_CALL logs_available(void*) noexcept;
    static uint64_t HA_CALL subscribe_logs(void*,HA_LogCallbackFn,uint32_t) noexcept;
    static int HA_CALL unsubscribe_logs(void*,uint64_t) noexcept;
    static uint64_t HA_CALL submit_job(void*,const HA_JobV1*) noexcept;
    static int HA_CALL cancel_job(void*,uint64_t) noexcept;
    static int HA_CALL post_editor(void*,HA_EditorCallbackFn,const char*,uint64_t) noexcept;
    static void HA_CALL addon_log(void* context, const char* message) noexcept;
    std::filesystem::path root_;
    std::vector<std::unique_ptr<Addon>> addons_;
    std::mutex log_mutex_;
    std::recursive_mutex callbacks_;
    bool started_ = false;
    bool factory_events_;
    std::string process_scope_;
};
bool plain_path(const std::filesystem::path& path);
}
