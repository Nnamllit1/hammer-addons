#pragma once
#include "hammer_jobs.h"
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <deque>
namespace ha {
struct JobDelivery {
    std::string owner, text;
    HA_EditorCallbackFn callback;
    uint64_t id, window;
    uint32_t state, progress;
};
class Jobs {
    struct Work;
    std::mutex mutex_;
    std::vector<std::shared_ptr<Work>> work_;
    std::deque<JobDelivery> posted_;
    std::set<uint64_t> windows_;
    uint64_t next_ = 1;
    bool stopped_ = false;
public:
    ~Jobs();
    uint64_t submit(const std::string& owner, const HA_JobV1& request);
    bool cancel(const std::string& owner, uint64_t id);
    bool post(const std::string& owner, HA_EditorCallbackFn callback, const char* text, uint64_t window);
    void window(uint64_t id, bool open);
    bool window_open(uint64_t id);
    void stop(const std::string& owner);
    void stop_all();
    std::vector<JobDelivery> take();
};
}
