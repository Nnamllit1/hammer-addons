#include "tool_logs.h"
#include "supported_tools.h"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
namespace ha {
// Private adapter for the inspected CS2 tier0 build. Interface order and context
// prefix are documented in AlliedModders' cs2 SDK public/tier0/logging.h.
// No engine context or C++ object is exposed through our public C ABI.
struct EngineLogContext {int channel,flags,severity;uint32_t color;const void* metadata;};
struct EngineListener {
    virtual void Log(const EngineLogContext*,const char*) noexcept=0;
    virtual void OnFlush() noexcept {}
    virtual void OnChannelRegistered(int) noexcept {}
    virtual void OnChannelVerbosityChanged(int) noexcept {}
    virtual void OnChannelFlagsChanged(int) noexcept {}
};
static bool supported(HMODULE module) {
    wchar_t path[32768]{};const auto length=GetModuleFileNameW(module,path,32768);
    if(!length || length>=32768)return false;
    std::ifstream in(std::filesystem::path(path),std::ios::binary);
    if(!in)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    struct Cleanup {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
       BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)return false;
    std::array<unsigned char,65536> data{};
    while(in) {
        in.read(reinterpret_cast<char*>(data.data()),data.size());
        if(in.gcount() && BCryptHashData(hash,data.data(),static_cast<ULONG>(in.gcount()),0)<0)return false;
    }
    if(in.bad())return false;
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return false;
    constexpr char hex[]="0123456789abcdef";std::string value;
    for(auto byte:digest){value+=hex[byte>>4];value+=hex[byte&15];}
    for(const auto* expected:supported_logging_hashes)if(value==expected)return true;
    return false;
}
struct ToolLogs::Buffer {
    std::mutex mutex;
    std::array<LogLine,256> lines;
    size_t begin=0,count=0;
    std::atomic<uint64_t> sequence{0},dropped{0};
    bool attached=false;
    void push(const EngineLogContext* context,const char* message) noexcept {
        if(!context || !message || (context->flags & 2))return;
        try {
            std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock.owns_lock() || count==lines.size()){++dropped;return;}
            const auto number=++sequence;
            auto& line=lines[(begin+count)%lines.size()];
            line.channel=context->channel;line.severity=static_cast<uint32_t>(context->severity);line.sequence=number;
            size_t length=strnlen_s(message,2049);line.truncated=length>2048;
            if(length>2048){length=2048;while(length && (static_cast<unsigned char>(message[length])&0xc0)==0x80)--length;}
            memcpy(line.text.data(),message,length);line.text[length]=0;++count;
        }catch(...){++dropped;}
    }
};
ToolLogs::ToolLogs():buffer_(std::make_shared<Buffer>()){}
bool ToolLogs::attach(HMODULE module) {
    if(buffer_->attached)return true;
    if(!module || !supported(module))return false;
    auto add=reinterpret_cast<void(__cdecl*)(EngineListener*)>(GetProcAddress(module,"LoggingSystem_RegisterLoggingListener"));
    if(!add || !GetProcAddress(module,"LoggingSystem_UnregisterLoggingListener"))return false;
    struct Listener final:EngineListener {
        std::shared_ptr<Buffer> buffer;
        explicit Listener(std::shared_ptr<Buffer> b):buffer(std::move(b)){}
        void Log(const EngineLogContext* context,const char* text) noexcept override {buffer->push(context,text);}
    };
    // Valve can copy listeners into nested logging states. Keep the listener and
    // its bounded buffer alive until process exit rather than freeing a pointer
    // another state may still hold. The loader also retains its runtime DLL.
    auto* listener=new Listener(buffer_);
    add(listener);buffer_->attached=true;return true;
}
bool ToolLogs::available() const {return buffer_->attached;}
uint64_t ToolLogs::latest() const {return buffer_->sequence.load();}
std::vector<LogLine> ToolLogs::take() {
    std::vector<LogLine> result;
    std::unique_lock lock(buffer_->mutex,std::try_to_lock);
    if(!lock.owns_lock())return result;
    const auto amount=std::min<size_t>(32,buffer_->count);result.reserve(amount);
    for(size_t i=0;i<amount;++i){auto line=buffer_->lines[buffer_->begin];line.dropped=buffer_->dropped.load();result.push_back(line);
        buffer_->begin=(buffer_->begin+1)%buffer_->lines.size();--buffer_->count;}
    return result;
}
}
