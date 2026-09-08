#include "runtime.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>

namespace fs = std::filesystem;
namespace ha {
bool plain_path(const fs::path& path) {
    // Reject junctions as well as symlinks, including ancestor directories.
    fs::path cursor;
    for (const auto& part : fs::absolute(path)) {
        cursor /= part;
        const DWORD flags = GetFileAttributesW(cursor.c_str());
        if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}
static std::string trim(std::string s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return {};
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}
static std::map<std::string, std::string> manifest(const fs::path& file) {
    if (!plain_path(file) || fs::file_size(file) > 16384) throw std::runtime_error("invalid manifest path or size");
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot read addon.ini");
    std::map<std::string, std::string> values;
    bool section = false;
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "[addon]" && !section) { section = true; continue; }
        auto pos = line.find('=');
        if (!section || pos == std::string::npos) throw std::runtime_error("invalid manifest syntax");
        auto key = trim(line.substr(0, pos));
        if (!values.emplace(key, trim(line.substr(pos + 1))).second) throw std::runtime_error("duplicate manifest key");
    }
    if (values.size() != 6 || values["format"] != "1" || values["abi"] != "1" ||
        !std::regex_match(values["id"], std::regex("[a-z][a-z0-9_]{0,63}")) ||
        !std::regex_match(values["version"], std::regex("[0-9]+\\.[0-9]+\\.[0-9]+")) ||
        (values["enabled"] != "true" && values["enabled"] != "false") ||
        !std::regex_match(values["entry"], std::regex("[a-zA-Z0-9_-]+\\.dll")))
        throw std::runtime_error("unsupported or incomplete addon.ini");
    return values;
}
Runtime::Runtime(fs::path root) : root_(std::move(root)) {}
void Runtime::log(const std::string& message) {
    std::lock_guard guard(log_mutex_);
    // Logs are best effort: a read-only installation must not break Hammer.
    const std::string line = "[hammer-addons] " + message;
    OutputDebugStringA((line + "\n").c_str());
    std::cout << line << std::endl;
    if (plain_path(root_ / "loader.log")) {
        std::ofstream out(root_ / "loader.log", std::ios::app);
        out << GetCurrentProcessId() << " " << line << '\n';
    }
}
void HA_CALL Runtime::addon_log(void* context, const char* message) {
    auto* addon = static_cast<Addon*>(context);
    if (message) addon->owner->log(addon->id + ": " + std::string(message).substr(0, 4096));
}
Summary Runtime::start() {
    std::lock_guard guard(callbacks_);
    if (started_) throw std::runtime_error("runtime already started");
    started_ = true;
    Summary result;
    if (!plain_path(root_)) throw std::runtime_error("loader directory contains a reparse point");
    if (fs::exists(root_ / "disabled")) { notice_ = "Add-ons disabled by the global marker file. Remove it and restart Workshop Tools to load add-ons."; log("disabled by marker file"); return result; }
    fs::path directory = root_ / "addons";
    if (!fs::exists(directory)) { log("no addons directory"); return result; }
    if (!plain_path(directory)) throw std::runtime_error("addons directory contains a reparse point");
    std::vector<fs::path> paths;
    for (const auto& item : fs::directory_iterator(directory)) if (item.is_directory()) paths.push_back(item.path());
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        statuses_.push_back({path.filename().string(), "", "Failed", ""});
        auto& status = statuses_.back();
        try {
            const auto data = manifest(path / "addon.ini");
            status.version = data.at("version");
            if (data.at("id") != path.filename().string()) throw std::runtime_error("id must equal folder name");
            if (data.at("enabled") == "false") { status.state = "Disabled"; status.detail = "Disabled in addon.ini"; ++result.disabled; continue; }
            const auto entry = fs::absolute(path / data.at("entry"));
            if (!plain_path(entry)) throw std::runtime_error("DLL path contains a reparse point");
            auto addon = std::make_unique<Addon>();
            addon->owner = this;
            addon->status_index = statuses_.size() - 1;
            addon->id = data.at("id");
            const auto utf8 = fs::absolute(path).u8string();
            addon->directory.assign(utf8.begin(), utf8.end());
            // No current-directory DLL search. Private dependencies belong beside the add-on.
            addon->module = LoadLibraryExW(entry.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!addon->module) throw std::runtime_error("LoadLibraryEx failed: " + std::to_string(GetLastError()));
            // Retain every loaded module until process exit, even on rejection. Its DllMain
            // may have registered callbacks; blindly unloading it would leave dangling code.
            auto* current = addon.get();
            addons_.push_back(std::move(addon));
            const auto query = reinterpret_cast<HA_QueryFn>(GetProcAddress(current->module, "HA_Query"));
            if (!query) throw std::runtime_error("HA_Query export missing");
            current->api = query(HA_ABI_VERSION);
            const auto* api = current->api;
            constexpr uint64_t capabilities = HA_CAP_LOGGING | HA_CAP_FACTORY_EVENTS;
            if (!api || api->size < sizeof(HA_AddonV1) || api->abi_version != HA_ABI_VERSION ||
                !api->id || current->id != api->id || !api->on_load || (api->required_capabilities & ~capabilities))
                throw std::runtime_error("add-on ABI, ID or required capabilities do not match");
            current->host = {sizeof(HA_HostV1), HA_ABI_VERSION, capabilities, current, addon_log, current->directory.c_str()};
            if (api->on_load(&current->host) != 1) throw std::runtime_error("on_load failed");
            current->active = true;
            status.state = "Loaded";
            status.detail = "Ready";
            ++result.loaded;
            log("loaded " + current->id + " " + data.at("version"));
        } catch (const std::exception& error) {
            ++result.rejected;
            status.detail = error.what();
            log("rejected " + path.filename().string() + ": " + error.what());
        } catch (...) {
            status.detail = "C++ exception";
            ++result.rejected;
            log("rejected " + path.filename().string() + ": C++ exception");
        }
    }
    return result;
}
void Runtime::event(const char* name, const char* value) {
    std::lock_guard guard(callbacks_);
    const HA_EventV1 event{sizeof(HA_EventV1), name, value};
    for (auto& addon : addons_) if (addon->active && addon->api->on_event) {
        try { addon->api->on_event(&event); }
        catch (...) { addon->active = false;
            auto& status = statuses_[addon->status_index];
            status.state = "Failed"; status.detail = "Event callback threw a C++ exception";
            log("disabled event callback after C++ exception: " + addon->id); }
    }
}
void Runtime::shutdown() {
    std::lock_guard guard(callbacks_);
    for (auto it = addons_.rbegin(); it != addons_.rend(); ++it) if ((*it)->active) {
        (*it)->active = false;
        statuses_[(*it)->status_index].state = "Stopped";
        try { if ((*it)->api->on_shutdown) (*it)->api->on_shutdown(); }
        catch (...) { log("shutdown callback threw: " + (*it)->id); }
    }
}
static std::string quote(const std::string& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += ch;
    }
    return out + '"';
}
void Runtime::initialization_error(const std::string& error) {
    std::lock_guard guard(callbacks_);
    notice_ = "Add-on initialization failed: " + error;
    log(notice_);
}
std::string Runtime::status_json() {
    // The UI must not block behind an add-on waiting for the editor thread.
    std::unique_lock guard(callbacks_, std::try_to_lock);
    if (!guard.owns_lock()) return {};
    const auto path = fs::absolute(root_ / "addons").u8string();
    std::string out = "{\"directory\":" + quote(std::string(path.begin(), path.end())) +
        ",\"notice\":" + quote(notice_) + ",\"addons\":[";
    bool first = true;
    for (const auto& status : statuses_) {
        if (!first) out += ',';
        first = false;
        out += "{\"id\":" + quote(status.id) + ",\"version\":" + quote(status.version) +
            ",\"state\":" + quote(status.state) + ",\"detail\":" + quote(status.detail) + "}";
    }
    return out + "]}";
}

}
