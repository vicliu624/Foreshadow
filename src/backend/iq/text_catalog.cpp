#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
namespace signalscope {
using Json=nlohmann::json;
std::filesystem::path application_directory() {
#ifdef _WIN32
    std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));path.resize(n);
    return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
    std::uint32_t size=0;_NSGetExecutablePath(nullptr,&size);std::string path(size,'\0');_NSGetExecutablePath(path.data(),&size);
    return std::filesystem::weakly_canonical(path.c_str()).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}
void TextCatalog::load(const std::filesystem::path& root,const std::vector<std::string>& directories,
                       const std::filesystem::path& boundary) {
        const auto base=std::filesystem::weakly_canonical(root);
        const auto packageRoot=boundary.empty()?base:std::filesystem::weakly_canonical(boundary);
        const auto baseWithin=base.lexically_relative(packageRoot);
        if(baseWithin.empty() || *baseWithin.begin()=="..")throw std::runtime_error("Catalog root is outside plugin package");
        for(const auto& name:directories) {
            const auto relative=path_from_utf8(name);
            if(relative.is_absolute())throw std::runtime_error("Provider catalog path must be relative");
            const auto directory=std::filesystem::weakly_canonical(base/relative);
            const auto within=directory.lexically_relative(packageRoot);
            if(within.empty() || *within.begin()==".." || !std::filesystem::is_directory(directory))
                throw std::runtime_error("Provider catalog directory missing or outside plugin package");
            for(const auto& entry:std::filesystem::directory_iterator(directory)) {
                if(entry.path().extension()!=".json" || !entry.is_regular_file())continue;
                if(entry.file_size()>1024*1024)throw std::runtime_error("Provider translation catalog is too large");
                std::ifstream input(entry.path());auto messages=Json::parse(input);
                if(!messages.is_object())throw std::runtime_error("Provider catalog must map text keys to strings");
                for(const auto& text:messages)if(!text.is_string())throw std::runtime_error("Invalid receiver catalog message");
                const auto locale=path_to_utf8(entry.path().stem());
                if(!messages_.contains(locale))messages_[locale]=Json::object();
                messages_[locale].update(messages);
            }
        }
    }
void TextCatalog::resolve(Json& node) const {
        if(node.is_array()){for(auto& value:node)resolve(value);return;}
        if(!node.is_object())return;
        for(auto& value:node)resolve(value);
        for(const auto* field:{"label","description","message"}) {
            const auto keyField=std::string(field)+"_key";
            if(!node.contains(keyField))continue;
            const auto key=node.at(keyField).get<std::string>();auto texts=Json::object();
            const auto& arguments=node.contains("arguments")?node.at("arguments"):node;
            for(const auto& [locale,messages]:messages_.items())if(messages.contains(key)) {
                auto text=messages.at(key).get<std::string>();
                for(const auto& [name,value]:arguments.items()) {
                    if(!value.is_primitive())continue;
                    const auto placeholder='{'+name+'}';const auto replacement=value.is_string()?value.get<std::string>():value.dump();
                    std::size_t pos=0;
                    while((pos=text.find(placeholder,pos))!=std::string::npos){text.replace(pos,placeholder.size(),replacement);pos+=replacement.size();}
                }
                texts[locale]=std::move(text);
            }
            if(!texts.empty()) {
                if(!texts.contains("en") && node.contains(field) && node.at(field).is_string())texts["en"]=node.at(field);
                node[field]=std::move(texts);
            }else if(!node.contains(field))node[field]=key;
        }
    }

std::filesystem::path resource_directory(const std::string& domain) {
    if(domain.empty() || domain.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos)
        throw std::invalid_argument("Invalid resource domain");
    std::vector<std::filesystem::path> roots;
    if(const auto overridePath=environment_utf8("SIGNALSCOPE_RESOURCE_PATH"))roots.push_back(path_from_utf8(*overridePath));
    else {
        const auto exe=application_directory();
        roots={exe/"resources",exe/"../resources",exe/"../share/signalscope",exe/"../Resources/signalscope"};
        auto parent=exe;
        for(int depth=0;depth<4 && !parent.empty();++depth,parent=parent.parent_path())roots.push_back(parent/"resources");
    }
    for(const auto& root:roots)if(std::filesystem::is_directory(root/domain))return root/domain;
    throw std::runtime_error("Resource domain is not installed: "+domain);
}
TextCatalog TextCatalog::resource(const std::string& domain) {
    TextCatalog result;result.load(resource_directory(domain),{"i18n"});return result;
}
}
