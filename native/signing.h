#pragma once
#include <filesystem>
#include <memory>
#include <string>
namespace ha::signing {
struct Identity { std::string name, contact, website; };
struct Verification {
    bool signed_package=false;
    std::string fingerprint, package_digest;
    Identity publisher;
    // Retain read-only file/directory handles until the loader opens the DLL.
    std::shared_ptr<void> locks, pin_locks;
};
std::string generate_key(const std::filesystem::path& key_file);
std::string export_key(const std::filesystem::path& local_key,const std::filesystem::path& portable_key,const std::string& password);
std::string import_key(const std::filesystem::path& portable_key,const std::filesystem::path& local_key,const std::string& password);
std::string sign(const std::filesystem::path& directory,const std::filesystem::path& key_file,const Identity& publisher,const std::string& password={});
void pin_publisher(const std::filesystem::path& directory,const std::filesystem::path& pin_directory,const std::string& expected);
bool check_publisher(const std::filesystem::path& pin_directory,const std::string& addon_id,Verification& verification);
Verification unsigned_snapshot(const std::filesystem::path& directory);
bool locally_approved(const std::filesystem::path& directory,const std::filesystem::path& approval_store,Verification& snapshot);
void approve_local(const std::filesystem::path& directory,const std::filesystem::path& approval_store);
Verification verify(const std::filesystem::path& directory);
}
