#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace signalscope {
inline std::vector<std::string> locale_fallbacks(std::string tag,bool includeSource=true) {
    std::replace(tag.begin(),tag.end(),'_','-');std::vector<std::string> result;
    while(!tag.empty()) {result.push_back(tag);const auto separator=tag.rfind('-');if(separator==std::string::npos)break;tag.resize(separator);}
    if(includeSource && std::find(result.begin(),result.end(),"en")==result.end())result.push_back("en");return result;
}
inline std::string localized_value(const nlohmann::json& translations,const std::vector<std::string>& locales,const std::string& fallback={}) {
    if(translations.is_string())return translations.get<std::string>();
    if(!translations.is_object())return fallback;
    const auto equal=[](const std::string& a,const std::string& b){return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](unsigned char x,unsigned char y){return std::tolower(x)==std::tolower(y);});};
    for(const auto& locale:locales)for(const auto& [key,value]:translations.items())if(equal(key,locale) && value.is_string())return value.get<std::string>();
    return fallback;
}
}
