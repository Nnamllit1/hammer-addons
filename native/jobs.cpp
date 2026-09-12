#include "jobs.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
namespace ha {
static std::string copy_text(const char* text, size_t limit=4096) {
    if(!text || strnlen_s(text,limit+1)>limit) throw std::runtime_error("invalid job text");
    return text;
}
struct Jobs::Work {
    std::string owner, input, text;
    HA_JobWorkFn work;
    HA_EditorCallbackFn notify;
    uint64_t id, window;
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::deque<std::string> messages;
    uint32_t progress=0, state=HA_JOB_PROGRESS;
    bool dirty=false, done=false;
    static int HA_CALL is_cancelled(void* p) noexcept {return static_cast<Work*>(p)->cancelled ? 1 : 0;}
    static int HA_CALL report(void* p,uint32_t value,const char* text) noexcept {
        try {
            auto& w=*static_cast<Work*>(p);
            if(value>100 || w.cancelled) return 0;
            auto copied=copy_text(text);
            std::unique_lock lock(w.mutex,std::try_to_lock);
            if(!lock.owns_lock() || w.done) return 0;
            w.text=std::move(copied);w.progress=value;w.dirty=true;return 1;
        } catch(...) {return 0;}
    }
    static int HA_CALL post(void* p,const char* text) noexcept {
        try {
            auto& w=*static_cast<Work*>(p);
            if(w.cancelled) return 0;
            auto copied=copy_text(text);
            std::unique_lock lock(w.mutex,std::try_to_lock);
            if(!lock.owns_lock() || w.done || w.messages.size()>=32) return 0;
            w.messages.push_back(std::move(copied));return 1;
        } catch(...) {return 0;}
    }
    void run() noexcept {
        char result[4097]{};
        uint32_t final=HA_JOB_FAILED;
        try {
            HA_JobContextV1 context{sizeof(context),this,is_cancelled,report,post};
            if(!cancelled) final=work(&context,input.c_str(),result,sizeof(result))==1 ? HA_JOB_SUCCEEDED : HA_JOB_FAILED;
        } catch(...) {strcpy_s(result,"Worker threw a C++ exception");}
        result[4096]=0;
        try {
            std::lock_guard lock(mutex);
            text=result;state=final;done=true;dirty=true;
        } catch(...) {
            // Terminal state still arrives if allocating the result failed.
            std::lock_guard lock(mutex);text.clear();state=HA_JOB_FAILED;done=true;dirty=true;
        }
    }
};
Jobs::~Jobs(){stop_all();}
uint64_t Jobs::submit(const std::string& owner,const HA_JobV1& request) {
    if(request.size<sizeof(request) || !request.work || !request.notify) return 0;
    auto input=copy_text(request.input,32768);
    std::lock_guard lock(mutex_);
    if(stopped_ || (request.window_id && !windows_.contains(request.window_id)) || work_.size()>=16 ||
       std::count_if(work_.begin(),work_.end(),[&](const auto& w){return w->owner==owner;})>=4) return 0;
    auto w=std::make_shared<Work>();
    w->owner=owner;w->input=std::move(input);w->work=request.work;w->notify=request.notify;w->id=next_++;w->window=request.window_id;
    work_.push_back(w);
    try {
        // Worker owns all its state. Shutdown cancels without joining on the GUI
        // thread; no worker retains a Runtime, Addon or Qt object pointer.
        std::thread([w]{w->run();}).detach();
    } catch(...) {work_.pop_back();return 0;}
    return w->id;
}
bool Jobs::cancel(const std::string& owner,uint64_t id) {
    std::lock_guard lock(mutex_);
    for(const auto& w:work_) if(w->owner==owner && w->id==id) {w->cancelled=true;return true;}
    return false;
}
bool Jobs::post(const std::string& owner,HA_EditorCallbackFn callback,const char* text,uint64_t window) {
    if(!callback)return false;
    auto copied=copy_text(text);
    std::lock_guard lock(mutex_);
    if(stopped_ || posted_.size()>=64 || (window && !windows_.contains(window))) return false;
    posted_.push_back({owner,std::move(copied),callback,0,window,HA_JOB_MESSAGE,0});return true;
}
void Jobs::window(uint64_t id,bool open) {
    if(!id)return;
    std::lock_guard lock(mutex_);
    if(open) {if(!stopped_) windows_.insert(id);return;}
    windows_.erase(id);
    // Keep cancelled workers counted against limits until they actually exit.
    for(const auto& w:work_) if(w->window==id) w->cancelled=true;
    std::erase_if(posted_,[&](const auto& p){return p.window==id;});
}
bool Jobs::window_open(uint64_t id) {std::lock_guard lock(mutex_);return !stopped_ && (!id || windows_.contains(id));}
bool Jobs::idle(const std::string& owner) {
    std::lock_guard lock(mutex_);
    // Completed workers must have their terminal deliveries drained too. This
    // prevents an old generation's notifications reaching a replacement instance.
    return std::none_of(work_.begin(),work_.end(),[&](const auto& w){return w->owner==owner;}) &&
        std::none_of(posted_.begin(),posted_.end(),[&](const auto& p){return p.owner==owner;});
}
void Jobs::stop(const std::string& owner) {
    std::lock_guard lock(mutex_);
    for(const auto& w:work_) if(w->owner==owner) w->cancelled=true;
    std::erase_if(posted_,[&](const auto& p){return p.owner==owner;});
}
void Jobs::stop_all() {
    std::lock_guard lock(mutex_);stopped_=true;
    for(const auto& w:work_)w->cancelled=true;
    work_.clear();posted_.clear();windows_.clear();
}
std::vector<JobDelivery> Jobs::take() {
    std::vector<JobDelivery> out;
    std::lock_guard lock(mutex_);
    for(auto it=work_.begin();it!=work_.end();) {
        auto& w=**it;
        std::unique_lock item(w.mutex,std::try_to_lock);
        if(!item.owns_lock()) {++it;continue;}
        const bool deliver=!w.window || windows_.contains(w.window);
        if(!deliver || w.cancelled)w.messages.clear();
        // One message per job per tick prevents a chatty worker monopolizing UI.
        if(deliver && !w.messages.empty()) {
            out.push_back({w.owner,w.messages.front(),w.notify,w.id,w.window,HA_JOB_MESSAGE,w.progress});
            w.messages.pop_front();
        } else if(deliver && w.dirty) {
            const auto state=w.done && w.cancelled ? HA_JOB_CANCELLED : w.state;
            out.push_back({w.owner,w.text,w.notify,w.id,w.window,state,w.progress});w.dirty=false;
        }
        if(w.done && w.messages.empty() && (!deliver || !w.dirty)) {
            item.unlock();it=work_.erase(it);
        } else ++it;
    }
    while(!posted_.empty() && out.size()<32) {out.push_back(std::move(posted_.front()));posted_.pop_front();}
    return out;
}
}
