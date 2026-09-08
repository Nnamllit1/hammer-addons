#include "runtime.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: ha_host <hammer-addons directory>\n"; return 2; }
    try {
        ha::Runtime runtime{argv[1]};
        auto result = runtime.start();
        runtime.event("host.test", "standalone");
        runtime.shutdown();
        std::cout << "SUMMARY loaded=" << result.loaded << " rejected=" << result.rejected << " disabled=" << result.disabled << '\n';
        return result.rejected ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
