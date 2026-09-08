#include "runtime.h"
#include <array>
#include <mutex>
#include <stdexcept>

static HMODULE self_module;
static HMODULE original_module;
static std::once_flag original_once, addons_once;
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
extern "C" FARPROC* HAProxyResolve(unsigned index) noexcept {
    try {
        std::call_once(original_once, [] {
            const auto file = module_directory() / "hammer_original.dll";
            if (!ha::plain_path(file)) throw std::runtime_error("original DLL path contains a reparse point");
            original_module = LoadLibraryExW(file.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!original_module) throw std::runtime_error("cannot load hammer_original.dll");
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
extern "C" void* __cdecl HACreateInterface(const char* name, int* result) noexcept {
    using Factory = void* (__cdecl *)(const char*, int*);
    auto factory = reinterpret_cast<Factory>(*HAProxyResolve(1));
    void* value = factory(name, result);
    // At this point Valve's factory has returned. No plugin work is done in DllMain.
    try {
        std::call_once(addons_once, [] {
            runtime = new ha::Runtime(module_directory() / "hammer-addons");
            runtime->start();
        });
        if (runtime && name) runtime->event("hammer.factory.request", name);
    } catch (...) {
        OutputDebugStringA("Hammer Addons: initialization failed; continuing original Hammer.\n");
    }
    return value;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self_module = instance;
    return TRUE;
}
