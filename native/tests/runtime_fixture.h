#pragma once
#include "runtime.h"
// Only the explicitly authored test packages receive automatic test approval.
// Production Runtime never grants approval and exposes no environment bypass.
class RuntimeFixture : public ha::Runtime {
    std::filesystem::path root_;
public:
    RuntimeFixture(std::filesystem::path root,std::filesystem::path settings={},bool factory=true,std::string scope={},ha::SteamExports steam={})
        :ha::Runtime(root,std::move(settings),factory,std::move(scope),steam),root_(std::move(root)){}
    ha::Summary start(){ha::review_addon_packages(root_,[](const ha::ApprovalRequest&){return true;});return ha::Runtime::start();}
};
