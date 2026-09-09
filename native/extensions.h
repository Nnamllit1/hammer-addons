#pragma once
#include "hammer_extensions.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace ha {
std::string json_quote(const std::string& value);
struct Control {
    uint32_t kind;
    std::string id, label, value, options;
};
struct Contribution {
    uint64_t handle;
    uint32_t kind;
    std::string owner, id, tool, label, target, options;
    std::vector<Control> controls;
    HA_InteractionFn callback;
    void* user;
};
Contribution copy_contribution(const std::string& owner, uint64_t handle, const HA_ContributionV1& input);
std::string contribution_json(const Contribution& contribution);
class Settings {
    std::filesystem::path root_;
    std::mutex mutex_;
public:
    explicit Settings(std::filesystem::path root = {});
    size_t get(const std::string& addon, const char* key, char* output, size_t capacity);
    bool set(const std::string& addon, const char* key, const char* value);
};
}
