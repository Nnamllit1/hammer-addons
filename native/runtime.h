#pragma once
#include "hammer_addons.h"
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
    explicit Runtime(std::filesystem::path root);
    Summary start();
    void event(const char* name, const char* value);
    void shutdown();
    std::string status_json();
    void initialization_error(const std::string& error);
    void log(const std::string& message);
private:
    struct Status { std::string id, version, state, detail; };
    std::vector<Status> statuses_;
    std::string notice_;
    struct Addon {
        Runtime* owner;
        std::string id, directory;
        HMODULE module = nullptr;
        const HA_AddonV1* api = nullptr;
        HA_HostV1 host{};
        bool active = false;
        size_t status_index = 0;
    };
    static void HA_CALL addon_log(void* context, const char* message);
    std::filesystem::path root_;
    std::vector<std::unique_ptr<Addon>> addons_;
    std::mutex log_mutex_;
    std::recursive_mutex callbacks_;
    bool started_ = false;
};
bool plain_path(const std::filesystem::path& path);
}
