#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace signalscope {
std::filesystem::path application_directory();
std::filesystem::path resource_directory(const std::string& domain);
class TextCatalog {
public:
    void load(const std::filesystem::path& root,const std::vector<std::string>& directories,
              const std::filesystem::path& boundary={});
    void resolve(nlohmann::json&) const;
    static TextCatalog resource(const std::string& domain);
private:
    nlohmann::json messages_=nlohmann::json::object();
};
}
