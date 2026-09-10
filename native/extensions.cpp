#include "extensions.h"
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <set>
#include <regex>
#include <stdexcept>
#include <windows.h>

namespace ha {
std::string json_quote(const std::string& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += ch;
    }
    return out + '"';
}
static std::string string(const char* value, size_t limit = 4096) {
    if (!value) return {};
    const auto size = strnlen_s(value, limit + 1);
    if (size > limit) throw std::runtime_error("extension string too long");
    return std::string(value, size);
}
static bool id(const std::string& value) { return std::regex_match(value, std::regex("[a-z][a-z0-9_]{0,63}")); }
Contribution copy_contribution(const std::string& owner, uint64_t handle, const HA_ContributionV1& in) {
    if (in.size < sizeof(in) || in.kind < HA_COMMAND || in.kind > HA_EDITOR_OBSERVER || !in.callback)
        throw std::runtime_error("invalid extension descriptor");
    Contribution c{handle, in.kind, owner, string(in.id, 64), string(in.tool, 32),
        string(in.label, 128), string(in.target, 512), string(in.options), {}, in.callback, in.user};
    static const std::set<std::string> tools{"all","project_picker","asset_browser","hammer","modeldoc","material_editor","particle_editor"};
    if (!id(c.id) || !tools.contains(c.tool) || c.label.empty() ||
        c.target.find("//") != std::string::npos || c.target.starts_with('/') || c.target.ends_with('/'))
        throw std::runtime_error("invalid extension ID, tool, label or target");
    if(c.kind==HA_EDITOR_OBSERVER && (!c.target.empty() || !c.options.empty()))
        throw std::runtime_error("editor observers do not have a menu target or options");
    if ((c.kind == HA_MENU_HOOK || c.kind == HA_IMPORT_ROUTE) && (c.target.empty() || c.tool == "all"))
        throw std::runtime_error("menu hooks need an explicit tool and action target");
    if ((c.kind == HA_IMPORTER || c.kind == HA_IMPORT_ROUTE) && !std::regex_match(c.options, std::regex("[a-z0-9]+(,[a-z0-9]+)*")))
        throw std::runtime_error("importer needs lowercase file extensions without dots");
    if (in.control_count > 64 || (in.control_count && !in.controls) ||
        (c.kind != HA_PANEL && in.control_count)) throw std::runtime_error("invalid controls");
    std::set<std::string> ids;
    for (uint32_t i = 0; i < in.control_count; ++i) {
        const auto& v = in.controls[i];
        if (v.size < sizeof(v)) throw std::runtime_error("short control descriptor");
        Control control{v.kind,string(v.id,64),string(v.label,128),string(v.initial_value),string(v.options)};
        if (v.size < sizeof(v) || v.kind < HA_LABEL || v.kind > HA_TEXT_VIEW || !id(control.id) ||
            !ids.insert(control.id).second) throw std::runtime_error("invalid control");
        if (v.kind == HA_CHECKBOX && control.value != "0" && control.value != "1")
            throw std::runtime_error("checkbox value must be 0 or 1");
        if (v.kind == HA_CHOICE) {
            auto count = 1 + std::count(control.options.begin(),control.options.end(),'\n');
            if (control.options.empty() || !std::regex_match(control.value,std::regex("[0-9]{1,4}")) ||
                std::stoul(control.value) >= static_cast<unsigned long>(count))
                throw std::runtime_error("invalid choice value");
        }
        c.controls.push_back(std::move(control));
    }
    return c;
}
std::string contribution_json(const Contribution& c) {
    std::string result = "{\"handle\":" + std::to_string(c.handle) + ",\"kind\":" + std::to_string(c.kind);
    for (const auto& [key,value] : std::vector<std::pair<std::string,std::string>>{
        {"owner",c.owner},{"id",c.id},{"tool",c.tool},{"label",c.label},{"target",c.target},{"options",c.options}})
        result += "," + json_quote(key) + ":" + json_quote(value);
    result += ",\"controls\":[";
    bool first = true;
    for (const auto& v : c.controls) {
        if (!first) result += ',';
        first = false;
        result += "{\"kind\":" + std::to_string(v.kind) + ",\"id\":" + json_quote(v.id) +
            ",\"label\":" + json_quote(v.label) + ",\"value\":" + json_quote(v.value) +
            ",\"options\":" + json_quote(v.options) + "}";
    }
    return result + "]}";
}
Settings::Settings(std::filesystem::path root) : root_(std::move(root)) {
    if (root_.empty()) {
        wchar_t path[32768]{};
        auto length = GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
        if (length && length < 32768) root_ = std::filesystem::path(path) / "HammerAddons" / "settings";
    }
}
size_t Settings::get(const std::string& addon, const char* key, char* output, size_t capacity) {
    try {
        std::lock_guard lock(mutex_);
        if (root_.empty() || !id(addon) || !key || !id(key)) return 0;
        const auto file = root_ / addon / (std::string(key) + ".txt");
        if (!plain_path(file)) return 0;
        std::string value;
        if (std::filesystem::exists(file)) {
            if (std::filesystem::file_size(file) > 4096) return 0;
            std::ifstream stream(file, std::ios::binary);
            if (!stream) return 0;
            value.assign(std::istreambuf_iterator<char>(stream), {});
        }
        if (output && capacity > value.size()) std::memcpy(output,value.c_str(),value.size()+1);
        return value.size()+1;
    } catch (...) { return 0; }
}
namespace {
// Own only the file created by this write, including cleanup after a failed rename.
struct PendingSetting {
    std::filesystem::path path;
    HANDLE stream = INVALID_HANDLE_VALUE;
    bool created = false;
    ~PendingSetting() {
        if (stream != INVALID_HANDLE_VALUE) CloseHandle(stream);
        if (created) DeleteFileW(path.c_str());
    }
};
}
bool Settings::set(const std::string& addon, const char* key, const char* raw) {
    try {
        std::lock_guard lock(mutex_);
        if (root_.empty() || !id(addon) || !key || !id(key) || !raw) return false;
        const auto value = string(raw);
        const auto file = root_ / addon / (std::string(key)+".txt");
        if (!plain_path(file)) return false;
        std::filesystem::create_directories(file.parent_path());
        static std::atomic<uint64_t> serial{0};
        PendingSetting pending;
        // A crashed writer's .pending file is not a lock. Independent sessions
        // publish their own sibling files; the last successful rename wins.
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            pending.path = file.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) +
                L"." + std::to_wstring(serial.fetch_add(1, std::memory_order_relaxed)) + L".pending";
            if (!plain_path(pending.path)) return false;
            pending.stream = CreateFileW(pending.path.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pending.stream != INVALID_HANDLE_VALUE) { pending.created = true; break; }
            const auto error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
        }
        if (!pending.created) return false;
        DWORD written = 0;
        const bool ok = WriteFile(pending.stream, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) &&
            written == value.size() && FlushFileBuffers(pending.stream);
        CloseHandle(pending.stream);
        pending.stream = INVALID_HANDLE_VALUE;
        if (!ok) return false;
        return MoveFileExW(pending.path.c_str(), file.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    } catch (...) { return false; }
}
}
