#include "signing.h"
#include <windows.h>
#include <iostream>
#include <map>
#include <stdexcept>
namespace fs=std::filesystem;
static std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(!s.empty()&&!n)throw std::runtime_error("Invalid Unicode argument");std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
struct Password {
    std::string value;
    ~Password(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}
    void read(const std::wstring& name){
        DWORD size=GetEnvironmentVariableW(name.c_str(),nullptr,0);
        if(size<2 || size>4097)throw std::runtime_error("Password environment variable is missing, empty or too long");
        std::wstring buffer(size,L'\0');
        struct Wipe {std::wstring& value;~Wipe(){SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));}} wipe{buffer};
        const auto copied=GetEnvironmentVariableW(name.c_str(),buffer.data(),size);
        if(!copied || copied>=size)throw std::runtime_error("Cannot read password environment variable");
        buffer.resize(copied);value=utf8(buffer);
    }
};
int wmain(int argc,wchar_t** argv){
    try {
        if(argc==1 || (argc==2 && std::wstring(argv[1])==L"--help")){
            std::cout<<"Offline Hammer Addons package signing (ECDSA P-256 / SHA-256)\n"
                "  addon_sign keygen <private.hakey>\n"
                "  addon_sign sign <addon-folder> --key <private.hakey> --name <publisher> [--contact <email>] [--website <URL>] [--password-env <name>]\n"
                "  addon_sign verify <addon-folder> [--publisher <64-character-fingerprint>]\n"
                "  addon_sign pin <addon-folder> --publisher <fingerprint> --store <loader-folder>/publisher-pins\n"
                "  addon_sign approve <addon-folder> --store <loader-folder>/local-approvals\n"
                "  addon_sign export-key <local.hakey> --output <portable.hapkey> --password-env <name>\n"
                "  addon_sign import-key <portable.hapkey> --output <local.hakey> --password-env <name>\n"
                "Local keys use Windows user protection; portable exports use a password. Keep both outside packages.\n"
                "Publisher details are self-declared; signatures do not establish safety or legal identity.\n";return 0;
        }
        if(argc<3)throw std::runtime_error("Missing command/path; use --help");
        const std::wstring command=argv[1];const fs::path path=argv[2];
        std::map<std::wstring,std::wstring> options;
        for(int i=3;i<argc;i+=2){if(i+1>=argc || !options.emplace(argv[i],argv[i+1]).second)throw std::runtime_error("Missing or duplicate option");}
        Password password;
        if(options.contains(L"--password-env"))password.read(options.at(L"--password-env"));
        if(command==L"export-key" || command==L"import-key"){
            if(options.size()!=2 || !options.contains(L"--output") || !options.contains(L"--password-env"))throw std::runtime_error("Key transfer needs --output and --password-env");
            const auto fingerprint=command==L"export-key"?ha::signing::export_key(path,options[L"--output"],password.value):ha::signing::import_key(path,options[L"--output"],password.value);
            std::cout<<"Publisher fingerprint: "<<fingerprint<<"\nKey transfer completed without changing publisher identity.\n";return 0;
        }
        if(command==L"keygen" && options.empty()){
            const auto fingerprint=ha::signing::generate_key(path);
            std::cout<<"Publisher fingerprint: "<<fingerprint<<"\nPrivate key created; retain it to sign future versions.\n";return 0;
        }
        if(command==L"sign"){
            for(const auto& [name,_]:options)if(name!=L"--key" && name!=L"--name" && name!=L"--contact" && name!=L"--website" && name!=L"--password-env")throw std::runtime_error("Unknown signing option");
            if(!options.contains(L"--key") || !options.contains(L"--name"))throw std::runtime_error("Signing needs --key and --name");
            ha::signing::Identity identity{utf8(options[L"--name"]),utf8(options[L"--contact"]),utf8(options[L"--website"])};
            const auto fingerprint=ha::signing::sign(path,options[L"--key"],identity,password.value);
            std::cout<<"Signed package. Publisher fingerprint: "<<fingerprint<<'\n';return 0;
        }
        if(command==L"approve"){
            if(options.size()!=1 || !options.contains(L"--store"))throw std::runtime_error("Approval needs --store");
            ha::signing::approve_local(path,options[L"--store"]);
            std::cout<<"Approved exact unsigned package locally for this Windows user. This does not establish publisher identity.\n";return 0;
        }
        if(command==L"pin"){
            if(options.size()!=2 || !options.contains(L"--publisher") || !options.contains(L"--store"))throw std::runtime_error("Pinning needs --publisher and --store");
            ha::signing::pin_publisher(path,options[L"--store"],utf8(options[L"--publisher"]));
            std::cout<<"Publisher pinned locally. Missing signatures and different keys will be rejected.\n";return 0;
        }
        if(command==L"verify"){
            for(const auto& [name,_]:options)if(name!=L"--publisher")throw std::runtime_error("Unknown verification option");
            const auto result=ha::signing::verify(path);
            if(!result.signed_package){std::cout<<"Unsigned package.\n";return 2;}
            if(options.contains(L"--publisher") && utf8(options[L"--publisher"])!=result.fingerprint)throw std::runtime_error("Publisher fingerprint does not match the expected key");
            std::cout<<"Valid package signature (not a safety endorsement).\nPublisher fingerprint: "<<result.fingerprint
                <<"\nClaimed name: "<<result.publisher.name<<"\nClaimed contact: "<<result.publisher.contact<<"\nClaimed website: "<<result.publisher.website<<'\n';return 0;
        }
        throw std::runtime_error("Unknown command/options; use --help");
    }catch(const std::exception& e){std::cerr<<"Signing error: "<<e.what()<<'\n';return 1;}
}
