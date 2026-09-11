#include "project.h"
#include "runtime.h"
#include <algorithm>
#include <cwctype>
#include <cstring>
namespace fs=std::filesystem;
namespace ha {
static std::string utf8(const fs::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
static bool component(const std::wstring& s) {
    if(s.empty() || s==L"." || s==L".." || s.back()==L'.' || s.back()==L' ')return false;
    for(wchar_t c:s)if(c<32 || std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos)return false;
    auto stem=s.substr(0,s.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towupper(c));});
    if(stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" || stem==L"CONIN$" || stem==L"CONOUT$")return false;
    if(stem.size()==4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
       (stem[3]>=L'1' && stem[3]<=L'9' || stem[3]==L'\u00b9' || stem[3]==L'\u00b2' || stem[3]==L'\u00b3'))return false;
    return true;
}
bool Project::initialize(const fs::path& executable,const std::vector<std::wstring>& arguments) {
    if(info_.size)return false;
    try {
        if(!executable.is_absolute() || _wcsicmp(executable.filename().c_str(),L"cs2.exe"))return false;
        auto binary=executable.parent_path();
        if(_wcsicmp(binary.filename().c_str(),L"win64") || _wcsicmp(binary.parent_path().filename().c_str(),L"bin") ||
           _wcsicmp(binary.parent_path().parent_path().filename().c_str(),L"game"))return false;
        std::wstring addon;bool tools=false,insecure=false;
        for(size_t i=0;i<arguments.size();++i) {
            if(arguments[i]==L"-tools")tools=true;
            if(arguments[i]==L"-insecure")insecure=true;
            if(arguments[i]==L"-addon") {
                if(!addon.empty() || i+1>=arguments.size() || !component(arguments[i+1]))return false;
                addon=arguments[++i];
            }
        }
        if(!tools || !insecure || addon.empty())return false;
        const auto root=binary.parent_path().parent_path().parent_path();
        const auto content=root/L"content/csgo_addons"/addon,game=root/L"game/csgo_addons"/addon;
        if(!plain_path(executable) || !fs::is_regular_file(executable) || !plain_path(content) || !plain_path(game) ||
           !fs::is_directory(content) || !fs::is_directory(game))return false;
        id_=utf8(addon);install_=utf8(root);content_=utf8(content);game_=utf8(game);
        info_={sizeof(info_),1,id_.c_str(),install_.c_str(),content_.c_str(),game_.c_str()};return true;
    }catch(...){return false;}
}
size_t Project::source(const char* relative,char* output,size_t capacity) const noexcept {
    try {
        if(!info_.size || !relative || strnlen_s(relative,4097)>4096)return 0;
        const auto rel=fs::path(std::u8string(reinterpret_cast<const char8_t*>(relative)));
        if(rel.empty() || rel.has_root_path())return 0;
        for(const auto& part:rel)if(!component(part.wstring()))return 0;
        const auto file=fs::path(std::u8string(reinterpret_cast<const char8_t*>(content_.c_str())))/rel;
        if(!plain_path(file) || !fs::is_regular_file(file))return 0;
        const auto value=utf8(file.lexically_normal());
        if(output && capacity>value.size())memcpy(output,value.c_str(),value.size()+1);
        return value.size()+1;
    }catch(...){return 0;}
}
}
