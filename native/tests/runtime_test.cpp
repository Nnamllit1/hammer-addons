#include "runtime.h"
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <thread>
#include <utility>

namespace fs = std::filesystem;
constexpr std::array capabilities{HA_CAP_LOGGING, HA_CAP_FACTORY_EVENTS, HA_CAP_UI,
    HA_CAP_SETTINGS, HA_CAP_MENU_HOOKS, HA_CAP_IMPORTERS, HA_CAP_EDITOR_EVENTS, HA_CAP_LIVE_PANELS};
static_assert([] {
    uint64_t seen = 0;
    for (auto bit : capabilities) {
        if (!bit || (bit & (bit - 1)) || (seen & bit)) return false;
        seen |= bit;
    }
    return true;
}(), "Every capability must have a distinct single bit");
static_assert(HA_CAP_IMPORTERS == 32 && HA_CAP_LIVE_PANELS == 64 && HA_CAP_EDITOR_EVENTS == 128);
static_assert(noexcept(std::declval<ha::Runtime&>().log("message")));
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
struct TemporaryDirectory {
    fs::path path;
    ~TemporaryDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};
struct ConsoleRedirect {
    std::streambuf* original;
    std::ios::iostate exceptions;
    explicit ConsoleRedirect(std::streambuf* replacement)
        : original(std::cout.rdbuf()), exceptions(std::cout.exceptions()) {
        std::cout.exceptions(std::ios::goodbit);
        std::cout.rdbuf(replacement);
        std::cout.exceptions(std::ios::badbit | std::ios::failbit);
    }
    ~ConsoleRedirect() {
        std::cout.exceptions(std::ios::goodbit);
        std::cout.rdbuf(original);
        std::cout.clear();
        std::cout.exceptions(exceptions);
    }
};
struct FailingConsole : std::streambuf {
    std::streamsize xsputn(const char*, std::streamsize) override { throw std::runtime_error("sink failure"); }
};
struct ReentrantConsole : std::streambuf {
    ha::Runtime& runtime;
    explicit ReentrantConsole(ha::Runtime& value) : runtime(value) {}
    std::streamsize xsputn(const char*, std::streamsize count) override { runtime.log("recursive log"); return count; }
    int_type overflow(int_type ch) override { runtime.log("recursive flush"); return ch; }
};
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "pass checkout root");
        const fs::path root = argv[1];
        TemporaryDirectory temp{root / "build/tests" / ("runtime-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()))};
        check(fs::create_directories(temp.path), "fresh temporary directory");
        ha::Settings settings(temp.path / "settings");
        check(settings.set("sample", "name", "before"), "initial write");
        const auto folder = temp.path / "settings/sample";
        const auto stale = folder / "name.txt.pending";
        std::ofstream(stale) << "interrupted write";
        check(settings.set("sample", "name", "after"), "stale pending must not block saving");
        check(read(stale) == "interrupted write", "do not delete another writer's pending file");
        check(read(folder / "name.txt") == "after", "replace persisted value");
        const auto held = CreateFileW(stale.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        check(held != INVALID_HANDLE_VALUE, "hold legacy pending file exclusively");
        const bool saved = settings.set("sample", "name", "held");
        CloseHandle(held);
        check(saved, "another writer's open pending file must not block saving");
        fs::create_directory(folder / "blocked.txt");
        check(!settings.set("sample", "blocked", "value"), "rename failure is reported");
        fs::remove(folder / "blocked.txt");
        check(settings.set("sample", "blocked", "recovered"), "failed rename must not poison future saves");
        for (const auto& file : fs::directory_iterator(folder))
            check(file.path().extension() != ".pending" || file.path() == stale, "temporary files cleaned up");
        std::atomic<unsigned> successful{0};
        std::vector<std::thread> writers;
        for (unsigned i = 0; i < 4; ++i) writers.emplace_back([&, i] {
            ha::Settings separate(temp.path / "settings");
            const std::string value(4096, static_cast<char>('a' + i));
            for (unsigned j = 0; j < 20; ++j) if (separate.set("sample", "shared", value.c_str())) ++successful;
        });
        for (auto& writer : writers) writer.join();
        const auto final = read(folder / "shared.txt");
        check(successful > 0 && final.size() == 4096 && final.find_first_not_of(final.front()) == std::string::npos,
            "concurrent writes publish a whole value");
        check(settings.set("sample", "shared", "last"), "save after concurrent writes succeeds");
        for (const auto& file : fs::directory_iterator(folder))
            check(file.path().extension() != ".pending" || file.path() == stale, "concurrent writer cleanup");
        ha::Runtime runtime(temp.path, temp.path / "settings");
        FailingConsole failing;
        {
            ConsoleRedirect redirect(&failing);
            runtime.log("file survives console failure");
            // Exercise the C ABI logging callback from the real example DLL.
            wchar_t executable[32768]{};
            check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "test executable path");
            fs::create_directories(temp.path / "addons/hello");
            fs::copy_file(fs::path(executable).parent_path() / "hello.dll", temp.path / "addons/hello/hello.dll");
            fs::copy_file(root / "addons/hello/addon.ini", temp.path / "addons/hello/addon.ini");
            check(runtime.start().loaded == 1, "logging sink failures must not reject add-ons");
            runtime.shutdown();
        }
        const auto log = read(temp.path / "loader.log");
        check(log.find("file survives console failure") != std::string::npos && log.find("Hello from") != std::string::npos,
            "file sink and ABI logging remain usable");
        ReentrantConsole recursive(runtime);
        { ConsoleRedirect redirect(&recursive); runtime.log("outer log"); }
        check(read(temp.path / "loader.log").find("outer log") != std::string::npos, "reentrant logger must return");
        fs::create_directory(temp.path / "unwritable");
        fs::create_directory(temp.path / "unwritable/loader.log");
        ha::Runtime invalidSink(temp.path / "unwritable", temp.path / "settings");
        invalidSink.log("file sink unavailable");
        std::cout << "Runtime regressions passed: capability bits, logging failures/reentrancy, stale pending files, rename recovery and concurrent saves.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
