#include "runtime.h"
#include <array>
#include <atomic>
#include <cstring>
#include "ui_bridge.h"
#include <mutex>
#include <stdexcept>
#include <thread>

#ifdef HA_ASSET_BROWSER
static constexpr const wchar_t* original_name = L"assetbrowser_original.dll";
static constexpr const char* factory_event = "tools.factory.request";
#else
static constexpr const wchar_t* original_name = L"hammer_original.dll";
static constexpr const char* factory_event = "hammer.factory.request";
#endif
static HMODULE self_module;
static HMODULE original_module;
static std::once_flag original_once;
static std::atomic_flag observing = ATOMIC_FLAG_INIT;
static bool addons_started = false; // Accessed only while holding observing.
struct ObservationGuard {
    ~ObservationGuard() { observing.clear(std::memory_order_release); }
};
static std::array<FARPROC, 6> exports{};
static ha::Runtime* runtime; // Process lifetime; no add-on cleanup under loader lock.
static constexpr const char* names[] = {"BinaryProperties_GetValue", "CreateInterface", "ExtractModuleMetadata",
    "GetResourceManifestCount", "GetResourceManifests", "InstallSchemaBindings"};

static std::filesystem::path module_directory() {
    std::wstring path(32768, L'\0');
    const auto count = GetModuleFileNameW(self_module, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count == path.size()) throw std::runtime_error("cannot locate proxy DLL");
    path.resize(count);
    return std::filesystem::path(path).parent_path();
}
static std::filesystem::path loader_directory() {
#ifdef HA_ASSET_BROWSER
    return module_directory() / "tools" / "hammer-addons";
#else
    return module_directory() / "hammer-addons";
#endif
}
extern "C" FARPROC* HAProxyResolve(unsigned index) noexcept {
    try {
        std::call_once(original_once, [] {
            const auto file = module_directory() / original_name;
            if (!ha::plain_path(file)) throw std::runtime_error("original DLL path contains a reparse point");
            original_module = LoadLibraryExW(file.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!original_module) throw std::runtime_error("cannot load original tools DLL");
            for (size_t i = 0; i < exports.size(); ++i) {
                exports[i] = GetProcAddress(original_module, names[i]);
                if (!exports[i]) throw std::runtime_error("original DLL export missing");
            }
        });
        return &exports.at(index);
    } catch (...) {
        OutputDebugStringA("Hammer Addons: original DLL unavailable; restore it using loader.py uninstall.\n");
        // Unknown export signatures cannot return a fabricated error value safely.
        RaiseFailFastException(nullptr, nullptr, 0);
        return nullptr;
    }
}
static size_t __cdecl read_status(void*, char* destination, size_t capacity) {
    try {
        auto json = runtime->status_json();
        if (json.empty()) return 0;
        if (destination && capacity >= json.size() + 1)
            std::memcpy(destination, json.c_str(), json.size() + 1);
        return json.size() + 1;
    } catch (...) { return 0; }
}
static void start_ui() {
    // Asset Browser can request its factory before QApplication exists. Retry from
    // a worker; HA_StartUi only queues widgets onto the application's GUI thread.
    std::thread([] {
        try {
            HMODULE module = nullptr;
            HA_UiStart start = nullptr;
            static const HA_UiHost host{sizeof(HA_UiHost), nullptr, read_status};
            for (unsigned attempt = 0; attempt < 120; ++attempt) {
                if (!module && GetModuleHandleW(L"Qt5Widgets.dll")) {
                    const auto file = loader_directory() / "hammer_addons_ui.dll";
                    if (!ha::plain_path(file)) { runtime->log("UI path contains a reparse point"); return; }
                    module = LoadLibraryExW(file.c_str(), nullptr,
                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
                    start = module ? reinterpret_cast<HA_UiStart>(GetProcAddress(module, "HA_StartUi")) : nullptr;
                    if (!start) { runtime->log("UI unavailable: cannot load UI entry point"); return; }
                }
                if (start && start(&host)) return;
                Sleep(500);
            }
            runtime->log("UI unavailable: no supported Qt application became ready");
        } catch (...) { OutputDebugStringA("Hammer Addons: UI startup failed.\n"); }
    }).detach();

}
extern "C" void* __cdecl HACreateInterface(const char* name, int* result) noexcept {
    using Factory = void* (__cdecl *)(const char*, int*);
    auto factory = reinterpret_cast<Factory>(*HAProxyResolve(1));
    void* value = factory(name, result);
    // Forward every call, but never wait on or recursively notify add-on callbacks.
    // A process-wide gate also covers on_load spawning and joining a factory caller.
    if (observing.test_and_set(std::memory_order_acquire)) return value;
    ObservationGuard guard;
    try {
        if (!addons_started) {
            addons_started = true;
            runtime = new ha::Runtime(loader_directory());
            try { runtime->start(); }
            catch (const std::exception& error) { runtime->initialization_error(error.what()); }
            catch (...) { runtime->initialization_error("C++ exception during initialization"); }
            start_ui();
        }
        if (runtime && name) runtime->event(factory_event, name);
    } catch (...) {
        OutputDebugStringA("Hammer Addons: initialization failed; continuing original tools.\n");
    }
    return value;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self_module = instance;
    return TRUE;
}
