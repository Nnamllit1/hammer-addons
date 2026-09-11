#pragma once
#include "hammer_project.h"
#include <filesystem>
#include <string>
#include <vector>
namespace ha {
class Project {
    std::string id_, install_, content_, game_;
    HA_ProjectInfoV1 info_{};
public:
    // Initialize once before add-ons load. Never update while callers borrow info.
    bool initialize(const std::filesystem::path& executable,const std::vector<std::wstring>& arguments);
    const HA_ProjectInfoV1* current() const noexcept {return info_.size ? &info_ : nullptr;}
    size_t source(const char* relative,char* output,size_t capacity) const noexcept;
};
}
