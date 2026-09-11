#include "owned_process.h"
#include "runtime.h"
#include "supported_tools.h"
#include <bcrypt.h>
#include <tlhelp32.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <array>
namespace fs=std::filesystem;
static fs::path own_folder() {
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(nullptr,path,32768)) throw std::runtime_error("Cannot locate launcher");
    return fs::path(path).parent_path();
}
static fs::path cs2_root(fs::path path) {
    path=fs::absolute(path).lexically_normal().make_preferred();
    if(fs::is_regular_file(path) && _wcsicmp(path.filename().c_str(),L"cs2.exe")==0) path=path.parent_path();
    if(fs::exists(path/L"cs2.exe") && path.filename()==L"win64") path=path.parent_path().parent_path().parent_path();
    return path;
}
static std::string hash(const fs::path& file) {
    std::ifstream stream(file,std::ios::binary);
    if(!stream) throw std::runtime_error("Cannot read original Asset Browser");
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE digest=nullptr;
    struct Cleanup {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,digest};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
        BCryptCreateHash(algorithm,&digest,nullptr,0,nullptr,0,0)<0) throw std::runtime_error("SHA256 unavailable");
    std::array<unsigned char,65536> block{};
    while(stream) {
        stream.read(reinterpret_cast<char*>(block.data()),block.size());
        if(BCryptHashData(digest,block.data(),static_cast<ULONG>(stream.gcount()),0)<0) throw std::runtime_error("SHA256 failed");
    }
    if(!stream.eof()) throw std::runtime_error("Cannot finish reading Asset Browser");
    unsigned char output[32]{};
    if(BCryptFinishHash(digest,output,sizeof(output),0)<0) throw std::runtime_error("SHA256 failed");
    std::string result;
    for(auto byte:output) {result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}
    return result;
}
static bool cs2_running() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot check running processes");
    struct Cleanup{HANDLE h;~Cleanup(){CloseHandle(h);}} cleanup{snapshot};
    PROCESSENTRY32W process{};process.dwSize=sizeof(process);
    if(!Process32FirstW(snapshot,&process)) throw std::runtime_error("Cannot enumerate running processes");
    do {if(!_wcsicmp(process.szExeFile,L"cs2.exe") || !_wcsicmp(process.szExeFile,L"csgocfg.exe"))return true;}while(Process32NextW(snapshot,&process));
    return false;
}
static fs::path detect(const fs::path& folder) {
    // A portable package can be next to the game, or CS2 can be found through Steam.
    std::vector<fs::path> candidates{folder,folder.parent_path()};
    wchar_t steam[32768]{};DWORD bytes=sizeof(steam);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&bytes)==ERROR_SUCCESS) {
        const fs::path root=steam;
        candidates.push_back(root/L"steamapps/common/Counter-Strike Global Offensive");
        std::ifstream file(root/L"steamapps/libraryfolders.vdf",std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(file),{}};
        const std::regex entry(R"vdf("path"\s+"((?:\\.|[^"\\])*)")vdf");
        for(std::sregex_iterator it(content.begin(),content.end(),entry),end;it!=end;++it) {
            const std::string raw=(*it)[1];std::string decoded;
            for(size_t i=0;i<raw.size();++i) {
                if(raw[i]=='\\' && i+1<raw.size()) ++i;
                decoded+=raw[i];
            }
            candidates.push_back(fs::path(std::u8string(reinterpret_cast<const char8_t*>(decoded.c_str())))/L"steamapps/common/Counter-Strike Global Offensive");
        }
    }
    for(const auto& path:candidates) if(fs::is_regular_file(path/L"game/bin/win64/cs2.exe")) return cs2_root(path);
    throw std::runtime_error("CS2 was not found. Drag its installation folder or cs2.exe onto Launch Workshop Tools.cmd.");
}
int wmain(int argc,wchar_t** argv) {
    try {
        fs::path root;bool check=false;
        for(int i=1;i<argc;++i) {
            const std::wstring arg=argv[i];
            if(arg==L"--help" || arg==L"-h") {
                std::cout<<"Workshop Tools with Add-ons\n\nDouble-click Launch Workshop Tools.cmd, or drop the CS2 folder onto it.\n"
                    "tools_launcher.exe [--cs2 <CS2 folder or cs2.exe>] [--check]\n"
                    "--check validates the package and game files without launching anything.\n";
                return 0;
            }
            if(arg==L"--check") check=true;
            else if(arg==L"--cs2" && i+1<argc) root=cs2_root(argv[++i]);
            else if(root.empty() && !arg.empty() && arg[0]!=L'-') root=cs2_root(arg);
            else throw std::runtime_error("Unknown or incomplete argument. Use --help.");
        }
        const auto folder=own_folder();
        if(root.empty()) root=detect(folder);
        auto rootText=root.wstring();
        if(!rootText.empty() && rootText.back()!=L'\\') rootText+=L'\\';
        if(!_wcsicmp(folder.c_str(),root.c_str()) || !_wcsnicmp(folder.c_str(),rootText.c_str(),rootText.size()))
            throw std::runtime_error("Keep the portable add-on package outside the CS2 installation folder.");
        const auto binary=root/L"game/bin/win64";
        const auto executable=binary/L"cs2.exe";
        const auto picker=binary/L"csgocfg.exe";
        const auto runtime=folder/L"hammer_addons_runtime.dll";
        if(!ha::plain_path(root) || !ha::plain_path(folder) || !ha::plain_path(executable) ||
            !ha::plain_path(picker) || !ha::plain_path(runtime) || !ha::plain_path(binary/L"assetbrowser.dll"))
            throw std::runtime_error("Linked/reparse paths are not supported");
        for(const auto& file:{executable,picker,binary/L"assetbrowser.dll",runtime,folder/L"hammer_addons_ui.dll"})
            if(!fs::is_regular_file(file)) throw std::runtime_error("Required file missing. Keep the complete portable package together and install CS2 Workshop Tools.");
        if(fs::exists(binary/L"tools/hammer-addons/install.json") || fs::exists(binary/L"assetbrowser_original.dll") ||
           fs::exists(binary/L"tools/hammer_original.dll"))
            throw std::runtime_error("An older replacement-DLL installation exists. Restore it with scripts/loader.py uninstall before using this launcher.");
        if(fs::exists(binary/L"cs2.exe.local")) throw std::runtime_error("Remove the existing cs2.exe.local override before launching.");
        const auto originalHash=hash(binary/L"assetbrowser.dll");
        bool supported=false;
        for(const auto* known:ha::supported_assetbrowser_hashes) supported|=originalHash==known;
        if(!supported) throw std::runtime_error("Asset Browser does not match a supported original Valve build (SHA256: "+originalHash+"). CS2 may have updated. Update Hammer Addons; if game files were modified, restore/verify them in Steam.");
        std::wcout<<L"CS2: "<<root.wstring()<<L"\nAdd-ons: "<<(folder/L"addons").wstring()<<L"\n";
        if(check) {std::cout<<"Original Valve Asset Browser verified. Portable package ready. No process started.\n";return 0;}
        if(cs2_running()) throw std::runtime_error("Close CS2 and Workshop Tools first. This launcher starts its own tools session and never attaches to an existing game.");
        // Review before starting processes: a human decision has no remote-start timeout.
        ha::review_addon_packages(folder,[](const ha::ApprovalRequest& request){
            const std::wstring id(request.id.begin(),request.id.end()),digest(request.digest.begin(),request.digest.end());
            if(!request.fingerprint.empty()){
                const auto publisher=fs::path(std::u8string(reinterpret_cast<const char8_t*>(request.publisher.c_str()))).wstring();
                const std::wstring fingerprint(request.fingerprint.begin(),request.fingerprint.end());
                const auto text=L"This package has a valid signature from a publisher key you have not approved.\n\nAdd-on: "+id+
                    L"\nClaimed publisher (identity not verified): "+publisher+L"\nPublisher SHA-256: "+fingerprint+
                    L"\n\nTrust this key for this add-on, including future updates signed with the same key? Native add-ons run with your Windows permissions. A signature is not proof of safety.";
                return MessageBoxW(nullptr,text.c_str(),L"Trust add-on publisher key?",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2|MB_SETFOREGROUND)==IDYES;
            }
            const auto message=L"This add-on has no publisher signature. Its author cannot be identified through a signature.\n\nAdd-on: "+id+
                L"\nFolder: "+request.directory.wstring()+L"\nPackage SHA-256: "+digest+
                L"\n\nNative add-ons run code with your Windows account's permissions. Only approve code you trust.\n\nApprove these exact package contents? This records your local approval, not a publisher certificate. Changed files will be blocked until explicitly reapproved.";
            return MessageBoxW(nullptr,message.c_str(),L"Approve unsigned Hammer add-on?",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2|MB_SETFOREGROUND)==IDYES;
        });
        // Steam opens Valve's project picker. Let it create the selected tools session.
        // Never supply -addon or consult the old launcher-project.txt preference.
        const std::vector<std::wstring> arguments{L"-steam",L"-retail",L"-gpuraytracing",L"-vulkan",L"-insecure",L"-nop4"};
        std::cout<<"Choose a project in the Workshop Tools window. Close it to cancel.\n";
        ha::ToolsProcess process(picker,arguments,true);
        if(!process.wait_for_tools(executable,runtime)) {std::cout<<"Workshop Tools launch cancelled.\n";return 0;}
        std::cout<<"Starting add-ons in the selected Workshop Tools session...\n";
        process.start_runtime(runtime);
        process.release();
        std::cout<<"Add-on runtime started in tools process "<<process.pid()<<".\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Cannot launch: "<<e.what()<<'\n';return 1;}
}
