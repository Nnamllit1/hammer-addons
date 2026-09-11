#include "signing.h"
#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
namespace ha::signing {
namespace {
using Bytes=std::vector<unsigned char>;
constexpr auto signature_name=L"addon.signature";
constexpr auto header="HAMMER-ADDONS-SIGNATURE-1\n";
constexpr auto key_header="HAMMER-ADDONS-PRIVATE-1\n";
constexpr size_t max_signature=1024*1024, max_files=1024;
constexpr uint64_t max_bytes=UINT64_C(1024)*1024*1024;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void ok(NTSTATUS status){require(status>=0,"Windows cryptography operation failed");}
struct Algorithm {
    BCRYPT_ALG_HANDLE handle=nullptr;
    explicit Algorithm(const wchar_t* name,ULONG flags=0){ok(BCryptOpenAlgorithmProvider(&handle,name,nullptr,flags));}
    ~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
};
struct Key {BCRYPT_KEY_HANDLE handle=nullptr;~Key(){if(handle)BCryptDestroyKey(handle);}};
struct Hash {
    Algorithm algorithm{BCRYPT_SHA256_ALGORITHM};BCRYPT_HASH_HANDLE handle=nullptr;
    Hash(){ok(BCryptCreateHash(algorithm.handle,&handle,nullptr,0,nullptr,0,0));}
    ~Hash(){if(handle)BCryptDestroyHash(handle);}
    void add(const void* p,size_t size){ok(BCryptHashData(handle,(PUCHAR)p,static_cast<ULONG>(size),0));}
    Bytes finish(){Bytes out(32);ok(BCryptFinishHash(handle,out.data(),32,0));return out;}
};
Bytes digest(const std::string& text){Hash h;h.add(text.data(),text.size());return h.finish();}
std::string hex(const unsigned char* p,size_t n){std::string out;out.reserve(n*2);constexpr char digits[]="0123456789abcdef";for(size_t i=0;i<n;++i){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;}
std::string hex(const Bytes& b){return hex(b.data(),b.size());}
std::string encoded(const std::string& s){return hex(reinterpret_cast<const unsigned char*>(s.data()),s.size());}
Bytes unhex(const std::string& text){
    require(text.size()%2==0,"Invalid signature hex encoding");Bytes out;out.reserve(text.size()/2);
    auto digit=[](char c)->unsigned char{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("Invalid signature hex encoding");};
    for(size_t i=0;i<text.size();i+=2)out.push_back(static_cast<unsigned char>(digit(text[i])*16+digit(text[i+1])));return out;
}
std::string decoded(const std::string& s){const auto b=unhex(s);return std::string(b.begin(),b.end());}
void text_field(const std::string& s,size_t max,bool required=false){
    require(s.size()<=max && (!required || !s.empty()),"Publisher field is missing or too long");
    require(s.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0)>0,"Publisher field is not UTF-8");
    for(unsigned char c:s)require(c>=32 && c!=127,"Control characters are not allowed in publisher fields");
}
struct Handles {
    std::vector<HANDLE> values;
    ~Handles(){for(auto h:values)CloseHandle(h);}
    HANDLE open(const fs::path& path,bool directory=false){
        const auto h=CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_FLAG_SEQUENTIAL_SCAN),nullptr);
        require(h!=INVALID_HANDLE_VALUE,"Cannot read or lock package file");
        try{values.push_back(h);}catch(...){CloseHandle(h);throw;}
        BY_HANDLE_FILE_INFORMATION info{};require(GetFileInformationByHandle(h,&info)!=0,"Cannot inspect package file");
        require(!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse points are not allowed in signed packages");
        require(bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory,"Package file type changed");
        if(!directory)require(info.nNumberOfLinks==1,"Hard-linked files are not allowed in signed packages");
        return h;
    }
};
Bytes read(HANDLE h,size_t max){
    LARGE_INTEGER size{};require(GetFileSizeEx(h,&size) && size.QuadPart>=0 && static_cast<uint64_t>(size.QuadPart)<=max,"Invalid file size");
    Bytes out(static_cast<size_t>(size.QuadPart));DWORD count=0;
    require(ReadFile(h,out.data(),static_cast<DWORD>(out.size()),&count,nullptr) && count==out.size(),"Cannot read file");return out;
}
void write_new(const fs::path& file,const Bytes& data){
    const auto h=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(h!=INVALID_HANDLE_VALUE,"Output already exists or cannot be created");
    DWORD count=0;const bool done=WriteFile(h,data.data(),static_cast<DWORD>(data.size()),&count,nullptr) && count==data.size() && FlushFileBuffers(h);
    CloseHandle(h);if(!done){DeleteFileW(file.c_str());throw std::runtime_error("Cannot write output");}
}
Bytes public_point(BCRYPT_KEY_HANDLE key){
    DWORD count=0;ok(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&count,0));Bytes blob(count);
    ok(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,blob.data(),count,&count,0));
    require(blob.size()==72,"Unexpected P-256 public key");Bytes point{4};point.insert(point.end(),blob.begin()+8,blob.end());return point;
}
std::string fingerprint(const Bytes& point){Hash h;h.add(point.data(),point.size());return hex(h.finish());}
void import_public(Algorithm& algorithm,Key& key,const Bytes& point){
    require(point.size()==65 && point[0]==4,"Invalid P-256 public key");
    Bytes blob(72);BCRYPT_ECCKEY_BLOB prefix{BCRYPT_ECDSA_PUBLIC_P256_MAGIC,32};memcpy(blob.data(),&prefix,8);memcpy(blob.data()+8,point.data()+1,64);
    ok(BCryptImportKeyPair(algorithm.handle,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.handle,blob.data(),static_cast<ULONG>(blob.size()),0));
}
void no_streams(const fs::path& path){
    WIN32_FIND_STREAM_DATA data{};HANDLE search=FindFirstStreamW(path.c_str(),FindStreamInfoStandard,&data,0);
    if(search==INVALID_HANDLE_VALUE){require(GetLastError()==ERROR_HANDLE_EOF,"Cannot inspect file streams");return;}
    bool valid=true;do {if(wcscmp(data.cStreamName,L"::$DATA")!=0 && wcscmp(data.cStreamName,L":Zone.Identifier:$DATA")!=0)valid=false;}while(FindNextStreamW(search,&data));
    const auto error=GetLastError();FindClose(search);require(error==ERROR_HANDLE_EOF && valid,"Unsupported alternate data stream in package");
}
using Inventory=std::map<std::string,std::string>;
Inventory inventory(const fs::path& root,Handles& locks){
    locks.open(root,true);Inventory files;uint64_t total=0;size_t nodes=0;
    for(const auto& entry:fs::recursive_directory_iterator(root)){
        require(++nodes<=max_files*2,"Package contains too many entries");
        const auto relative=entry.path().lexically_relative(root).generic_u8string();const std::string name(relative.begin(),relative.end());
        // Conservative portable names avoid Windows aliases, case collisions and stream paths.
        require(name.size()<=240 && !name.empty(),"Package path is too long");
        size_t start=0;for(size_t i=0;i<=name.size();++i){
            if(i==name.size() || name[i]=='/'){
                const auto part=name.substr(start,i-start);require(!part.empty() && part!="." && part!=".." && part.back()!='.' && part.back()!=' ',"Invalid package path component");start=i+1;
            }else {const auto c=static_cast<unsigned char>(name[i]);require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==' ',"Signed package paths must use portable ASCII names");}
        }
        if(entry.is_directory()){locks.open(entry.path(),true);continue;}
        require(entry.is_regular_file(),"Only regular files are allowed in packages");
        auto lower=name;for(char& c:lower)if(c>='A'&&c<='Z')c+=32;
        if(lower=="addon.signature"){require(name=="addon.signature","Signature filename must be lowercase");continue;}
        require(files.size()<max_files,"Package contains too many files");
        const auto h=locks.open(entry.path());no_streams(entry.path());
        LARGE_INTEGER size{};require(GetFileSizeEx(h,&size) && size.QuadPart>=0,"Cannot read package size");
        total+=static_cast<uint64_t>(size.QuadPart);require(total<=max_bytes,"Signed package exceeds 1 GiB");
        Hash hash;std::array<unsigned char,65536> buffer{};DWORD count=0;
        do{require(ReadFile(h,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)!=0,"Cannot hash package file");if(count)hash.add(buffer.data(),count);}while(count);
        require(files.emplace(name,hex(hash.finish())).second,"Duplicate package file");
    }
    require(files.contains("addon.ini"),"Package must contain addon.ini");
    std::set<std::string> names;for(const auto& [name,_]:files){auto lower=name;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;require(names.insert(lower).second,"Case-colliding package files");}
    return files;
}
std::string payload(const Bytes& point,const Identity& identity,const Inventory& files){
    text_field(identity.name,128,true);text_field(identity.contact,256);text_field(identity.website,512);
    std::string out=std::string(header)+"key="+hex(point)+"\nname="+encoded(identity.name)+"\ncontact="+encoded(identity.contact)+"\nwebsite="+encoded(identity.website)+"\n";
    for(const auto& [name,hash]:files)out+="file="+hash+" "+encoded(name)+"\n";return out;
}
struct Secret {Secret()=default;Secret(const Secret&)=delete;Secret(Secret&&)=default;Bytes data;~Secret(){if(!data.empty())SecureZeroMemory(data.data(),data.size());}};
struct Protected {DATA_BLOB data{};~Protected(){if(data.pbData){SecureZeroMemory(data.pbData,data.cbData);LocalFree(data.pbData);}}};

constexpr auto portable_header="HAMMER-ADDONS-PORTABLE-KEY-1\n";
Secret private_blob(const fs::path& file,const std::string& password);
Bytes portable_crypt(const Bytes& private_key,const std::string& password,const Bytes* encrypted){
    const size_t prefix=strlen(portable_header),private_size=104;
    require(!password.empty() && password.size()<=4096,"A password is required for this portable key (maximum 4096 UTF-8 bytes)");
    Bytes blob;
    if(encrypted){blob=*encrypted;require(blob.size()==prefix+16+12+private_size+16 && memcmp(blob.data(),portable_header,prefix)==0,"Invalid portable key format");}
    else {
        require(private_key.size()==private_size && password.size()>=16,"Export requires a P-256 key and a password of at least 16 UTF-8 bytes");
        blob.resize(prefix+16+12+private_size+16);memcpy(blob.data(),portable_header,prefix);
        ok(BCryptGenRandom(nullptr,blob.data()+prefix,28,BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    }
    Secret derived;derived.data.resize(32);Algorithm pbkdf(BCRYPT_SHA256_ALGORITHM,BCRYPT_ALG_HANDLE_HMAC_FLAG);
    ok(BCryptDeriveKeyPBKDF2(pbkdf.handle,(PUCHAR)password.data(),static_cast<ULONG>(password.size()),blob.data()+prefix,16,600000,derived.data.data(),32,0));
    Algorithm aes(BCRYPT_AES_ALGORITHM);
    ok(BCryptSetProperty(aes.handle,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0));
    Key key;ok(BCryptGenerateSymmetricKey(aes.handle,&key.handle,nullptr,0,derived.data.data(),32,0));
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce=blob.data()+prefix+16;auth.cbNonce=12;
    auth.pbAuthData=blob.data();auth.cbAuthData=static_cast<ULONG>(prefix+16+12);
    auth.pbTag=blob.data()+prefix+28+private_size;auth.cbTag=16;
    DWORD count=0;
    if(encrypted){
        Secret output;output.data.resize(private_size);
        require(BCryptDecrypt(key.handle,blob.data()+prefix+28,static_cast<ULONG>(private_size),&auth,nullptr,0,output.data.data(),static_cast<ULONG>(private_size),&count,0)>=0 && count==private_size,
            "Portable key password is incorrect or the encrypted key was modified");
        return std::move(output.data);
    }
    ok(BCryptEncrypt(key.handle,(PUCHAR)private_key.data(),static_cast<ULONG>(private_size),&auth,nullptr,0,blob.data()+prefix+28,static_cast<ULONG>(private_size),&count,0));
    require(count==private_size,"Unexpected encrypted key size");return blob;
}
Secret private_blob(const fs::path& file,const std::string& password){
    Handles locks;const auto encrypted=read(locks.open(file),16384);Secret result;
    if(encrypted.size()>strlen(portable_header) && memcmp(encrypted.data(),portable_header,strlen(portable_header))==0){
        result.data=portable_crypt({},password,&encrypted);return result;
    }
    require(encrypted.size()>strlen(key_header) && memcmp(encrypted.data(),key_header,strlen(key_header))==0,"Not a Hammer Addons protected key");
    DATA_BLOB cipher{static_cast<DWORD>(encrypted.size()-strlen(key_header)),const_cast<BYTE*>(encrypted.data()+strlen(key_header))};Protected plain;
    require(CryptUnprotectData(&cipher,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.data)!=0,"Cannot unlock local key for this Windows user; export a portable key on the original machine");
    result.data.assign(plain.data.pbData,plain.data.pbData+plain.data.cbData);return result;
}
void save_local_key(const fs::path& file,const Bytes& bytes){
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),(BYTE*)bytes.data()};Protected encrypted;
    require(CryptProtectData(&input,L"Hammer Addons publisher key",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data)!=0,"Cannot protect publisher key");
    Bytes output(key_header,key_header+strlen(key_header));output.insert(output.end(),encrypted.data.pbData,encrypted.data.pbData+encrypted.data.cbData);write_new(file,output);
}
std::string private_fingerprint(const Bytes& blob){
    Algorithm algorithm(BCRYPT_ECDSA_P256_ALGORITHM);Key key;
    ok(BCryptImportKeyPair(algorithm.handle,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.handle,(PUCHAR)blob.data(),static_cast<ULONG>(blob.size()),0));return fingerprint(public_point(key.handle));
}
}
std::string export_key(const fs::path& local,const fs::path& portable,const std::string& password){
    const auto key=private_blob(local,{});const auto fingerprint=private_fingerprint(key.data);
    write_new(portable,portable_crypt(key.data,password,nullptr));return fingerprint;
}
std::string import_key(const fs::path& portable,const fs::path& local,const std::string& password){
    const auto key=private_blob(portable,password);const auto fingerprint=private_fingerprint(key.data);
    save_local_key(local,key.data);return fingerprint;
}
std::string generate_key(const fs::path& file){
    Algorithm algorithm(BCRYPT_ECDSA_P256_ALGORITHM);Key key;ok(BCryptGenerateKeyPair(algorithm.handle,&key.handle,256,0));ok(BCryptFinalizeKeyPair(key.handle,0));
    DWORD count=0;ok(BCryptExportKey(key.handle,nullptr,BCRYPT_ECCPRIVATE_BLOB,nullptr,0,&count,0));Secret secret;secret.data.resize(count);
    ok(BCryptExportKey(key.handle,nullptr,BCRYPT_ECCPRIVATE_BLOB,secret.data.data(),count,&count,0));
    DATA_BLOB plain{count,secret.data.data()};Protected encrypted;
    require(CryptProtectData(&plain,L"Hammer Addons publisher key",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data)!=0,"Cannot protect publisher key");
    Bytes output(key_header,key_header+strlen(key_header));output.insert(output.end(),encrypted.data.pbData,encrypted.data.pbData+encrypted.data.cbData);write_new(file,output);
    return fingerprint(public_point(key.handle));
}
std::string sign(const fs::path& directory,const fs::path& file,const Identity& identity,const std::string& password){
    const auto root=fs::canonical(directory),key_path=fs::canonical(file);
    for(auto parent=key_path.parent_path();!parent.empty();){require(!fs::equivalent(parent,root),"Keep the private key outside the add-on package");auto next=parent.parent_path();if(next==parent)break;parent=next;}
    const auto plain=private_blob(file,password);
    Algorithm algorithm(BCRYPT_ECDSA_P256_ALGORITHM);Key key;ok(BCryptImportKeyPair(algorithm.handle,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.handle,(PUCHAR)plain.data.data(),static_cast<ULONG>(plain.data.size()),0));
    Handles locks;const auto files=inventory(root,locks);
    const auto point=public_point(key.handle);auto body=payload(point,identity,files);auto hash=digest(body);Bytes signature(64);DWORD count=0;
    ok(BCryptSignHash(key.handle,nullptr,hash.data(),32,signature.data(),64,&count,0));require(count==64,"Unexpected P-256 signature size");
    body+="signature="+hex(signature)+"\n";
    // Never overwrite a prior signature implicitly. Sign a fresh staging package.
    write_new(root/signature_name,Bytes(body.begin(),body.end()));return fingerprint(point);
}
Verification verify(const fs::path& directory){
    Verification result;const auto path=directory/signature_name;
    const auto flags=GetFileAttributesW(path.c_str());
    if(flags==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();require(error==ERROR_FILE_NOT_FOUND,"Cannot inspect package signature");return result;}
    auto locks=std::make_shared<Handles>();const auto bytes=read(locks->open(path),max_signature);no_streams(path);
    const std::string text(bytes.begin(),bytes.end());require(text.starts_with(header),"Unsupported package signature format");
    size_t pos=strlen(header);
    auto field=[&](const std::string& prefix){const auto end=text.find('\n',pos);require(end!=std::string::npos && text.compare(pos,prefix.size(),prefix)==0,"Malformed package signature");auto value=text.substr(pos+prefix.size(),end-pos-prefix.size());pos=end+1;return value;};
    const auto point=unhex(field("key="));result.publisher.name=decoded(field("name="));result.publisher.contact=decoded(field("contact="));result.publisher.website=decoded(field("website="));
    Inventory files;
    while(text.compare(pos,5,"file=")==0){const auto line=field("file=");require(line.size()>65 && line[64]==' ',"Malformed signed inventory");const auto hash=line.substr(0,64);require(unhex(hash).size()==32,"Invalid file hash");require(files.size()<max_files && files.emplace(decoded(line.substr(65)),hash).second,"Duplicate or excessive inventory entry");}
    const auto body=text.substr(0,pos);const auto signature=unhex(field("signature="));require(pos==text.size() && signature.size()==64,"Malformed signature trailer");
    require(body==payload(point,result.publisher,files),"Noncanonical package signature");
    Algorithm algorithm(BCRYPT_ECDSA_P256_ALGORITHM);Key key;import_public(algorithm,key,point);auto hash=digest(body);
    require(BCryptVerifySignature(key.handle,nullptr,hash.data(),32,const_cast<PUCHAR>(signature.data()),64,0)>=0,"Package signature does not verify");
    require(files==inventory(directory,*locks),"Signed package files have changed, are missing, or were added");
    result.signed_package=true;result.fingerprint=fingerprint(point);result.locks=std::move(locks);return result;
}
namespace {
void addon_id(const std::string& id){
    require(!id.empty() && id.size()<=64 && id[0]>='a' && id[0]<='z',"Invalid add-on ID for publisher pin");
    for(char c:id)require((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_',"Invalid add-on ID for publisher pin");
}
}
void pin_publisher(const fs::path& directory,const fs::path& pin_directory,const std::string& expected){
    const auto result=verify(directory);
    require(result.signed_package && result.fingerprint==expected,"Package must match the explicitly supplied publisher fingerprint");
    const auto id=directory.filename().string();addon_id(id);
    fs::create_directories(pin_directory);Handles directory_lock;directory_lock.open(pin_directory,true);
    const auto text=expected+"\n";write_new(pin_directory/(id+".sha256"),Bytes(text.begin(),text.end()));
}
bool check_publisher(const fs::path& pin_directory,const std::string& id,Verification& verification){
    addon_id(id);
    if(!fs::exists(pin_directory))return false;
    auto locks=std::make_shared<Handles>();locks->open(pin_directory,true);
    const auto path=pin_directory/(id+".sha256");
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect publisher pin");return false;}
    const auto bytes=read(locks->open(path),65);require(bytes.size()==65 && bytes.back()=='\n',"Malformed publisher pin");
    const std::string expected(bytes.begin(),bytes.end()-1);require(unhex(expected).size()==32,"Malformed publisher fingerprint");
    require(verification.signed_package,"This add-on requires its pinned publisher signature");
    require(verification.fingerprint==expected,"Publisher key differs from the locally pinned key");
    verification.pin_locks=std::move(locks);return true;
}

Verification unsigned_snapshot(const fs::path& directory){
    require(!fs::exists(directory/signature_name),"Local approval cannot override a publisher signature");
    Verification result;auto locks=std::make_shared<Handles>();const auto files=inventory(directory,*locks);
    std::string payload="HAMMER-ADDONS-LOCAL-PACKAGE-1\n";
    for(const auto& [name,hash]:files)payload+="file="+hash+" "+encoded(name)+"\n";
    result.package_digest=hex(digest(payload));result.locks=std::move(locks);return result;
}
namespace {
std::string approval_text(const fs::path& directory,const Verification& snapshot){
    const auto id=directory.filename().string();addon_id(id);
    return "HAMMER-ADDONS-LOCAL-APPROVAL-1\nid="+id+"\npackage="+snapshot.package_digest+"\n";
}
void save_approval(const fs::path& directory,const fs::path& store,const Verification& snapshot){
    const auto text=approval_text(directory,snapshot);
    DATA_BLOB input{static_cast<DWORD>(text.size()),(BYTE*)text.data()};Protected encrypted;
    require(CryptProtectData(&input,L"Hammer Addons local package approval",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data)!=0,"Cannot protect local approval");
    fs::create_directories(store);Handles directory_lock;directory_lock.open(store,true);
    std::array<unsigned char,16> random{};ok(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    const auto pending=store/("approval-"+hex(random.data(),random.size())+".pending");
    write_new(pending,Bytes(encrypted.data.pbData,encrypted.data.pbData+encrypted.data.cbData));
    const auto target=store/(directory.filename().string()+".approval");
    if(!MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(pending.c_str());throw std::runtime_error("Cannot save local approval; package remains unapproved");
    }
}
}
bool locally_approved(const fs::path& directory,const fs::path& store,Verification& snapshot){
    const auto expected=approval_text(directory,snapshot);
    if(!fs::exists(store))return false;
    auto locks=std::make_shared<Handles>();locks->open(store,true);
    const auto file=store/(directory.filename().string()+".approval");
    const auto attributes=GetFileAttributesW(file.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect local approval");return false;}
    const auto encrypted=read(locks->open(file),16384);
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),const_cast<BYTE*>(encrypted.data())};Protected plain;
    require(CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.data)!=0,"Local approval is invalid or belongs to another Windows user; explicit reapproval is required");
    require(plain.data.cbData==expected.size() && memcmp(plain.data.pbData,expected.data(),expected.size())==0,
        "Package changed since local approval; review it and explicitly reapprove with addon_sign approve");
    snapshot.pin_locks=std::move(locks);return true;
}
void approve_local(const fs::path& directory,const fs::path& store){
    const auto snapshot=unsigned_snapshot(directory);save_approval(directory,store,snapshot);
}

}
