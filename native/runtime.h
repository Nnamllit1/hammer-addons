#pragma once
#include "hammer_addons.h"
#include "extensions.h"
#include "hammer_editor.h"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ha {
struct Summary { unsigned loaded = 0, rejected = 0, disabled = 0; };
class Runtime {
public:
    explicit Runtime(std::filesystem::path root, std::filesystem::path settings_root = {}, bool factory_events = true, std::string process_scope = {});
    Summary start();
    void event(const char* name, const char* value);
    void shutdown();
    std::string status_json();
    int invoke(uint64_t handle, const char* tool, const char* phase, const char* control,
               const char* value, char* response, size_t capacity, const HA_EditorStateV1* editor = nullptr);
    void report_binding(uint64_t handle, const char* tool, const char* message);
    void initialization_error(const std::string& error);
    void log(const std::string& message);
private:
    struct Status { std::string id, version, state, detail; std::vector<std::string> tools; };
    std::vector<Status> statuses_;
    std::string notice_;
    Settings settings_;
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
        size_t status_index = 0;
    };
    static uint64_t HA_CALL register_contribution(void*, const HA_ContributionV1*);
    static size_t HA_CALL get_setting(void*, const char*, char*, size_t);
    static int HA_CALL set_setting(void*, const char*, const char*);
    static int HA_CALL set_panel_text(void*,uint64_t,const char*,const char*);
    static void HA_CALL addon_log(void* context, const char* message);
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
