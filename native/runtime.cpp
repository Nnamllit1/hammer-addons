#include "runtime.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <set>
#include <sstream>

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
static std::vector<std::string> tool_tags(const std::string& value) {
    static const std::set<std::string> allowed{"all", "project_picker", "asset_browser", "hammer", "modeldoc", "material_editor", "particle_editor"};
    std::vector<std::string> tags;
    std::istringstream input(value);
    for (std::string tag; std::getline(input, tag, ',');) {
        tag = trim(tag);
        if (!allowed.contains(tag) || std::find(tags.begin(), tags.end(), tag) != tags.end())
            throw std::runtime_error("invalid or duplicate tools tag");
        tags.push_back(tag);
    }
    if (tags.empty() || value.back() == ',' ||
        (tags.size() > 1 && std::find(tags.begin(), tags.end(), "all") != tags.end()))
        throw std::runtime_error("tools must list supported tool IDs, or all alone");
    return tags;
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
    for (const auto& [key, value] : values) {
        if (key != "format" && key != "abi" && key != "id" && key != "version" &&
            key != "enabled" && key != "entry" && key != "tools")
            throw std::runtime_error("unknown manifest key");
    }
    if ((values.size() != 6 && values.size() != 7) || values["format"] != "1" || values["abi"] != "1" ||
        !std::regex_match(values["id"], std::regex("[a-z][a-z0-9_]{0,63}")) ||
        !std::regex_match(values["version"], std::regex("[0-9]+\\.[0-9]+\\.[0-9]+")) ||
        (values["enabled"] != "true" && values["enabled"] != "false") ||
        !std::regex_match(values["entry"], std::regex("[a-zA-Z0-9_-]+\\.dll")))
        throw std::runtime_error("unsupported or incomplete addon.ini");
    if (values.contains("tools")) tool_tags(values.at("tools"));
    return values;
}
Runtime::Runtime(fs::path root, fs::path settings_root, bool factory_events, std::string process_scope, SteamExports steam_exports) : settings_(std::move(settings_root)), steam_(steam_exports), root_(std::move(root)), factory_events_(factory_events), process_scope_(std::move(process_scope)) {}
void Runtime::log(std::string_view message) noexcept {
    // A failing or reentrant sink must never unwind through the host ABI or
    // prevent runtime initialization. Drop contended messages instead of waiting.
    static thread_local bool logging = false;
    if (logging) return;
    logging = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{logging};
    try {
        std::unique_lock guard(log_mutex_, std::try_to_lock);
        if (!guard.owns_lock()) return;
        const std::string line = "[hammer-addons] " + std::string(message);
        OutputDebugStringA((line + "\n").c_str());
        try { std::cout << line << std::endl; } catch (...) {}
        // Console failure must not suppress the file sink.
        try {
            if (plain_path(root_ / "loader.log")) {
                std::ofstream out(root_ / "loader.log", std::ios::app);
                out << GetCurrentProcessId() << " " << line << '\n';
            }
        } catch (...) {}
    } catch (...) {}
}
void HA_CALL Runtime::addon_log(void* context, const char* message) noexcept {
    try {
        auto* addon = static_cast<Addon*>(context);
        if (addon && message)
            addon->owner->log(addon->id + ": " + std::string(message, strnlen_s(message, 4096)));
    } catch (...) {}
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
        statuses_.push_back({path.filename().string(), "", "Failed", "", {}});
        auto& status = statuses_.back();
        try {
            const auto data = manifest(path / "addon.ini");
            status.version = data.at("version");
            if (data.contains("tools")) status.tools = tool_tags(data.at("tools"));
            if (data.at("id") != path.filename().string()) throw std::runtime_error("id must equal folder name");
            if (data.at("enabled") == "false") { status.state = "Disabled"; status.detail = "Disabled in addon.ini"; ++result.disabled; continue; }
            // Picker opt-in is explicit: existing all/untagged editor add-ons must
            // not suddenly execute in a different application after an update.
            const bool picker_tag=std::find(status.tools.begin(),status.tools.end(),"project_picker")!=status.tools.end();
            if((process_scope_=="project_picker" && !picker_tag) ||
               (process_scope_=="tools" && picker_tag && status.tools.size()==1)) {
                status.state="Skipped";status.detail="Not enabled for this application";continue;
            }
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
            const uint64_t capabilities = HA_CAP_LOGGING | (factory_events_ ? HA_CAP_FACTORY_EVENTS : 0) | HA_CAP_UI | HA_CAP_SETTINGS | HA_CAP_MENU_HOOKS | HA_CAP_IMPORTERS | HA_CAP_EDITOR_EVENTS | HA_CAP_LIVE_PANELS | HA_CAP_JOBS | HA_CAP_EDITOR_QUEUE | HA_CAP_TOOL_LOGS | HA_CAP_BUILD_OUTPUT | HA_CAP_PROJECT_CONTEXT | HA_CAP_TABLES | HA_CAP_STEAM;
            if (!api || api->size < sizeof(HA_AddonV1) || api->abi_version != HA_ABI_VERSION ||
                !api->id || current->id != api->id || !api->on_load || (api->required_capabilities & ~capabilities))
                throw std::runtime_error("add-on ABI, ID or required capabilities do not match");
            static const HA_ProjectV1 project_api{sizeof(HA_ProjectV1),1,current_project,source_path};
            static const HA_SteamV1 steam_api{sizeof(HA_SteamV1),1,steam_snapshot,steam_friend,steam_profile,steam_friends,subscribe_steam,unsubscribe_steam};
            static const HA_LogsV1 log_api{sizeof(HA_LogsV1),1,logs_available,subscribe_logs,unsubscribe_logs};
            static const HA_JobsV1 job_api{sizeof(HA_JobsV1),1,submit_job,cancel_job,post_editor};
            static const HA_ExtensionsV1 extension_api{sizeof(HA_ExtensionsV1), HA_EXTENSIONS_VERSION,
                register_contribution, get_setting, set_setting, set_panel_text, &job_api, &log_api, &project_api, &steam_api};
            current->host = {sizeof(HA_HostV1), HA_ABI_VERSION, capabilities, current, addon_log, current->directory.c_str(), &extension_api};
            current->registering = true;
            int loaded = 0;
            try { loaded = api->on_load(&current->host); }
            catch (...) { current->registering = false; throw; }
            current->registering = false;
            if (loaded != 1) throw std::runtime_error("on_load failed");
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
    std::unique_lock guard(callbacks_, std::try_to_lock);
    if (!guard.owns_lock() || dispatching_) return;
    dispatching_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{dispatching_};
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
    jobs_.stop_all();
    for (auto it = addons_.rbegin(); it != addons_.rend(); ++it) if ((*it)->active) {
        (*it)->active = false;
        statuses_[(*it)->status_index].state = "Stopped";
        try { if ((*it)->api->on_shutdown) (*it)->api->on_shutdown(); }
        catch (...) { log("shutdown callback threw: " + (*it)->id); }
    }
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
    std::string out = "{\"directory\":" + json_quote(std::string(path.begin(), path.end())) +
        ",\"notice\":" + json_quote(notice_) + ",\"addons\":[";
    bool first = true;
    for (const auto& status : statuses_) {
        if (!first) out += ',';
        first = false;
        out += "{\"id\":" + json_quote(status.id) + ",\"version\":" + json_quote(status.version) +
            ",\"state\":" + json_quote(status.state) + ",\"detail\":" + json_quote(status.detail) + ",\"tools\":[";
        for (size_t i = 0; i < status.tools.size(); ++i) {
            if (i) out += ',';
            out += json_quote(status.tools[i]);
        }
        out += "]}";
    }
    out += "],\"contributions\":[";
    first = true;
    for (const auto& addon : addons_) if (addon->active) {
        for (const auto& contribution : addon->contributions) {
            if (!first) out += ',';
            first = false;
            out += contribution_json(contribution);
        }
    }
    return out + "]}";
}

uint64_t HA_CALL Runtime::register_contribution(void* context, const HA_ContributionV1* definition) {
    auto* addon = static_cast<Addon*>(context);
    try {
        std::unique_lock guard(addon->owner->callbacks_, std::try_to_lock);
        if (!guard.owns_lock() || !addon->registering || !definition || addon->contributions.size() >= 128) return 0;
        auto contribution = copy_contribution(addon->id, addon->owner->next_handle_, *definition);
        for (const auto& existing : addon->contributions) if (existing.id == contribution.id) return 0;
        addon->contributions.push_back(std::move(contribution));
        return addon->owner->next_handle_++;
    } catch (const std::exception& error) {
        addon->owner->log(addon->id + ": extension registration rejected: " + error.what());
        return 0;
    } catch (...) { return 0; }
}
size_t HA_CALL Runtime::get_setting(void* context, const char* key, char* output, size_t capacity) {
    auto* addon = static_cast<Addon*>(context);
    return addon->owner->settings_.get(addon->id, key, output, capacity);
}
int HA_CALL Runtime::set_setting(void* context, const char* key, const char* value) {
    auto* addon = static_cast<Addon*>(context);
    return addon->owner->settings_.set(addon->id, key, value) ? 1 : 0;
}
int HA_CALL Runtime::set_panel_text(void* context,uint64_t panel,const char* control,const char* value) {
    auto* addon=static_cast<Addon*>(context);
    try {
        std::unique_lock lock(addon->owner->callbacks_,std::try_to_lock);
        if(!lock.owns_lock() || (!addon->active && !addon->registering) || !control || !value ||
           strnlen_s(control,65)>64 || strnlen_s(value,4097)>4096) return 0;
        for(auto& c:addon->contributions) if(c.handle==panel && c.kind==HA_PANEL)
            for(auto& v:c.controls) if(v.id==control && (v.kind==HA_LABEL || v.kind==HA_TEXT_VIEW || v.kind==HA_TABLE)) {
                if(v.kind==HA_TABLE && !valid_table(value,v.options))return 0;
                v.value=value;return 1;
            }
    } catch(...) {}
    return 0;
}
int Runtime::invoke(uint64_t handle, const char* tool, const char* phase, const char* control,
                    const char* value, char* response, size_t capacity, const HA_EditorStateV1* editor, const HA_BuildOutputV1* build) {
    if (response && capacity) response[0] = 0;
    std::unique_lock guard(callbacks_, std::try_to_lock);
    if (!guard.owns_lock() || dispatching_) return HA_BUSY;
    if (!tool || !phase || !control || !value) return HA_ERROR;
    for (auto& addon : addons_) if (addon->active) {
        for (const auto& c : addon->contributions) {
            if (c.handle != handle || (c.tool != "all" && c.tool != tool)) continue;
            const std::string event_phase(phase);
            if ((c.kind == HA_COMMAND && event_phase != "command") ||
                ((c.kind == HA_IMPORTER || c.kind == HA_IMPORT_ROUTE) && event_phase != "import") ||
                (c.kind == HA_MENU_HOOK && event_phase != "hook.before" && event_phase != "hook.after") ||
                (c.kind == HA_PANEL && event_phase != "panel.change" && event_phase != "panel.click")) return HA_ERROR;
            if(c.kind==HA_EDITOR_OBSERVER && (!editor || editor->size<sizeof(HA_EditorStateV1) ||
                (event_phase!="editor.opened" && event_phase!="editor.changed" && event_phase!="editor.closed"))) return HA_ERROR;
            if(c.kind!=HA_EDITOR_OBSERVER && editor) return HA_ERROR;
            if(c.kind==HA_BUILD_OBSERVER && (!build || build->size<sizeof(HA_BuildOutputV1) ||
                !build->window_id || !build->stream_id || !build->sequence || !build->title || !build->text ||
                (build->flags & ~HA_BUILD_TRUNCATED) || strnlen_s(build->title,4097)>4096 ||
                strnlen_s(build->text,131073)>131072 ||
                (event_phase!="build.output" && event_phase!="build.closed"))) return HA_ERROR;
            if(c.kind!=HA_BUILD_OBSERVER && build) return HA_ERROR;
            if (c.kind == HA_PANEL) {
                const auto found = std::find_if(c.controls.begin(),c.controls.end(),[&](const Control& v) { return v.id == control; });
                if (found == c.controls.end() || (found->kind == HA_LABEL || found->kind == HA_TEXT_VIEW) ||
                    (event_phase == "panel.click") != (found->kind == HA_BUTTON)) return HA_ERROR;
            }
            if(c.kind==HA_PANEL) {
                for(const auto& v:c.controls)if(v.id==control && v.kind==HA_TABLE) {
                    // Ignore selections from a stale UI snapshot after a row's removal.
                    if(event_phase!="panel.change" || !*value || strnlen_s(value,65)>64)return HA_ERROR;
                    const std::string key=std::string(value)+"\t";
                    if(!v.value.starts_with(key) && v.value.find("\n"+key)==std::string::npos)return HA_ERROR;
                }
            }
            if (c.kind == HA_IMPORTER || c.kind == HA_IMPORT_ROUTE) {
                const auto file = fs::path(std::u8string(reinterpret_cast<const char8_t*>(value)));
                if (!file.is_absolute() || !fs::is_regular_file(file)) return HA_ERROR;
                auto ext = file.extension().string();
                std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (ext.size() < 2 || ("," + c.options + ",").find("," + ext.substr(1) + ",") == std::string::npos) return HA_ERROR;
            }
            dispatching_ = true;
            struct Reset { bool& value; ~Reset() { value = false; } } reset{dispatching_};
            const HA_InteractionV1 event{sizeof(HA_InteractionV1),tool,phase,control,value,editor,build};
            // Overlay requests are scoped to this add-on's direct user action.
            steam_action_owner_=(editor_thread_==GetCurrentThreadId() &&
                (event_phase=="command" || event_phase=="panel.click")) ? addon.get() : nullptr;
            struct ResetAction {Addon*& owner;~ResetAction(){owner=nullptr;}} reset_action{steam_action_owner_};
            try {
                const int result = c.callback(c.user,&event,response,capacity);
                if (response && capacity) response[capacity-1] = 0;
                return result == HA_CONTINUE || result == HA_HANDLED ? result : HA_ERROR;
            } catch (...) {
                addon->active = false;
                auto& state = statuses_[addon->status_index];
                state.state = "Failed";
                state.detail = "Extension callback threw a C++ exception";
                log(addon->id + ": " + state.detail);
                return HA_ERROR;
            }
        }
    }
    return HA_ERROR;
}
void Runtime::report_binding(uint64_t handle, const char* tool, const char* message) {
    log("UI binding " + std::to_string(handle) + " [" + (tool ? tool : "") + "]: " + (message ? message : ""));
}

}

namespace ha {
uint64_t HA_CALL Runtime::submit_job(void* context,const HA_JobV1* request) noexcept {
    try {
        auto* a=static_cast<Addon*>(context);if(!a || !request)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        if(!lock.owns_lock() || !a->active)return 0;
        return a->owner->jobs_.submit(a->id,*request);
    }catch(...){return 0;}
}
int HA_CALL Runtime::cancel_job(void* context,uint64_t id) noexcept {
    try {
        auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && a->active && a->owner->jobs_.cancel(a->id,id);
    }catch(...){return 0;}
}
int HA_CALL Runtime::post_editor(void* context,HA_EditorCallbackFn callback,const char* text,uint64_t window) noexcept {
    try {
        auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && a->active && a->owner->jobs_.post(a->id,callback,text,window);
    }catch(...){return 0;}
}
void Runtime::editor_window(uint64_t id,bool open) noexcept {
    try {jobs_.window(id,open);}catch(...){}
}
void Runtime::pump_jobs() noexcept {
    try {
        std::unique_lock lock(callbacks_,std::try_to_lock);
        if(!lock.owns_lock() || dispatching_)return;
        if(!editor_thread_)editor_thread_=GetCurrentThreadId();
        if(editor_thread_!=GetCurrentThreadId())return;
        dispatching_=true;
        struct Reset{bool& value;~Reset(){value=false;}} reset{dispatching_};
        if((process_scope_=="tools" || process_scope_=="project_picker") && GetTickCount64()>=next_steam_poll_) {
            next_steam_poll_=GetTickCount64()+1000;steam_.refresh();
        }
        for(const auto& a:addons_)if(a->active) {
            const auto subscriptions=a->steam_subscriptions;
            for(const auto& subscription:subscriptions) {
                auto current=std::find_if(a->steam_subscriptions.begin(),a->steam_subscriptions.end(),[&](const auto& s){return s.id==subscription.id;});
                if(!a->active || current==a->steam_subscriptions.end() || current->after==steam_.state().revision)continue;
                current->after=steam_.state().revision;
                try {subscription.callback(&steam_.state());}
                catch(...) {a->active=false;jobs_.stop(a->id);auto& status=statuses_[a->status_index];
                    status.state="Failed";status.detail="Steam callback threw a C++ exception";log(status.detail);}
            }
        }
        for(const auto& a:addons_)if(!a->active)jobs_.stop(a->id);
        for(const auto& line:tool_logs_.take()) {
            for(const auto& a:addons_)if(a->active) {
                const auto subscriptions=a->logs;
                for(const auto& subscription:subscriptions) {
                    if(!a->active || line.sequence<=subscription.after || line.severity<subscription.minimum ||
                       std::none_of(a->logs.begin(),a->logs.end(),[&](const auto& s){return s.id==subscription.id;}))continue;
                    const HA_LogEventV1 event{sizeof(event),line.severity,line.channel,line.truncated,line.sequence,line.dropped,line.text.data()};
                    try {subscription.callback(&event);}
                    catch(...) {
                        a->active=false;jobs_.stop(a->id);
                        auto& status=statuses_[a->status_index];status.state="Failed";status.detail="Tool log callback threw a C++ exception";
                    }
                }
            }
        }

        for(const auto& delivery:jobs_.take()) {
            if(!jobs_.window_open(delivery.window))continue;
            for(const auto& a:addons_)if(a->active && a->id==delivery.owner) {
                const HA_JobEventV1 event{sizeof(event),delivery.state,delivery.progress,delivery.id,delivery.text.c_str()};
                try {delivery.callback(&event);}
                catch(...) {
                    a->active=false;jobs_.stop(a->id);
                    auto& status=statuses_[a->status_index];status.state="Failed";
                    status.detail="Queued editor callback threw a C++ exception";log(status.detail);
                }
                break;
            }
        }
    }catch(...){log("Job dispatch failed");}
}
}

namespace ha {
bool Runtime::attach_tool_logs(HMODULE module) {std::lock_guard lock(callbacks_);return tool_logs_.attach(module);}
int HA_CALL Runtime::logs_available(void* context) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && a->owner->tool_logs_.available();
    }catch(...){return 0;}
}
uint64_t HA_CALL Runtime::subscribe_logs(void* context,HA_LogCallbackFn callback,uint32_t minimum) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a || !callback || minimum>HA_LOG_ERROR)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        if(!lock.owns_lock() || (!a->active && !a->registering) || a->logs.size()>=8 || !a->owner->tool_logs_.available())return 0;
        const auto id=a->owner->next_subscription_++;a->logs.push_back({id,callback,minimum,a->owner->tool_logs_.latest()});return id;
    }catch(...){return 0;}
}
int HA_CALL Runtime::unsubscribe_logs(void* context,uint64_t id) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);if(!lock.owns_lock())return 0;
        return std::erase_if(a->logs,[&](const auto& s){return s.id==id;})!=0;
    }catch(...){return 0;}
}
}

namespace ha {
bool Runtime::initialize_project(const fs::path& executable,const std::vector<std::wstring>& args) {
    return !started_ && project_.initialize(executable,args);
}
const HA_ProjectInfoV1* HA_CALL Runtime::current_project(void* context) noexcept {
    auto* addon=static_cast<Addon*>(context);
    return addon ? addon->owner->project_.current() : nullptr;
}
size_t HA_CALL Runtime::source_path(void* context,const char* path,char* output,size_t capacity) noexcept {
    auto* addon=static_cast<Addon*>(context);
    return addon ? addon->owner->project_.source(path,output,capacity) : 0;
}
}

namespace ha {
int HA_CALL Runtime::steam_snapshot(void* context,HA_SteamStateV1* output) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a || !output || output->size<sizeof(*output))return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        if(!lock.owns_lock() || (!a->active && !a->registering))return 0;
        *output=a->owner->steam_.state();return 1;
    }catch(...){return 0;}
}
int HA_CALL Runtime::steam_friend(void* context,uint64_t revision,uint32_t index,HA_SteamFriendV1* output) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a || !output || output->size<sizeof(*output))return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && (a->active || a->registering) && a->owner->steam_.friend_at(revision,index,*output);
    }catch(...){return 0;}
}
int HA_CALL Runtime::steam_profile(void* context,uint64_t id) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && a->active && a->owner->steam_action_owner_==a &&
            a->owner->editor_thread_==GetCurrentThreadId() && a->owner->steam_.open_profile(id);
    }catch(...){return 0;}
}
int HA_CALL Runtime::steam_friends(void* context) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && a->active && a->owner->steam_action_owner_==a &&
            a->owner->editor_thread_==GetCurrentThreadId() && a->owner->steam_.open_friends();
    }catch(...){return 0;}
}
uint64_t HA_CALL Runtime::subscribe_steam(void* context,HA_SteamCallbackFn callback) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a || !callback)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        if(!lock.owns_lock() || (!a->active && !a->registering) || a->steam_subscriptions.size()>=4)return 0;
        const auto id=a->owner->next_subscription_++;a->steam_subscriptions.push_back({id,callback,0});return id;
    }catch(...){return 0;}
}
int HA_CALL Runtime::unsubscribe_steam(void* context,uint64_t id) noexcept {
    try {auto* a=static_cast<Addon*>(context);if(!a)return 0;
        std::unique_lock lock(a->owner->callbacks_,std::try_to_lock);
        return lock.owns_lock() && std::erase_if(a->steam_subscriptions,[id](const auto& s){return s.id==id;})!=0;
    }catch(...){return 0;}
}
}
