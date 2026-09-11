#pragma once
#include "hammer_logs.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
namespace ha {
struct LogLine {
    uint32_t severity=0, truncated=0;
    int32_t channel=0;
    uint64_t sequence=0, dropped=0;
    std::array<char,2049> text{};
};
class ToolLogs {
    struct Buffer;
    std::shared_ptr<Buffer> buffer_;
public:
    ToolLogs();
    bool attach(HMODULE module);
    bool available() const;
    uint64_t latest() const;
    std::vector<LogLine> take();
};
}
