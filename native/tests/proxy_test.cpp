#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
static void check(bool ok) { if (!ok) throw std::runtime_error("forwarding mismatch"); }
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    try {
        const auto file = std::filesystem::absolute(argv[1]);
        auto dll = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(dll != nullptr);
        auto integers = reinterpret_cast<uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t)>(GetProcAddress(dll,"BinaryProperties_GetValue"));
        auto floats = reinterpret_cast<double(*)(double,double,double,double,double)>(GetProcAddress(dll,"ExtractModuleMetadata"));
        auto mixed = reinterpret_cast<uint64_t(*)(uint64_t,double,uint64_t,double,uint64_t)>(GetProcAddress(dll,"GetResourceManifests"));
        auto count = reinterpret_cast<int(*)()>(GetProcAddress(dll,"GetResourceManifestCount"));
        auto schema = reinterpret_cast<void(*)(uint64_t*)>(GetProcAddress(dll,"InstallSchemaBindings"));
        auto factory = reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(dll,"CreateInterface"));
        check(integers && floats && mixed && count && schema && factory);
        std::vector<std::thread> threads;
        for (int i=0; i<8; ++i) threads.emplace_back([&] { for (int n=0;n<100;++n) if(count()!=17) std::abort(); });
        for (auto& thread : threads) thread.join();
        check(integers(1,2,3,4,5,6)==183);
        check(floats(1,2,3,4,5)==55);
        check(mixed(1,2,3,4,5)==55);
        uint64_t value=0; schema(&value); check(value==0xfedcba9876543210ULL);
        int result=-1; check(factory("ToolSystem2_001",&result)==reinterpret_cast<void*>(static_cast<uintptr_t>(0x12345678)) && result==0);
        check(factory("missing",&result)==nullptr && result==1);
        for (uintptr_t ordinal=1;ordinal<=6;++ordinal) check(GetProcAddress(dll,reinterpret_cast<const char*>(ordinal))!=nullptr);
        std::cout << "All six exports forwarded; integers, floats, mixed arguments, stack arguments, ordinals and concurrent resolution passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
