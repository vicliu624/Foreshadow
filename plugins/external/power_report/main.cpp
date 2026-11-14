#include "catalog.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/iq/locale.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>

namespace {
std::uint64_t parseSample(const std::string& value) {
    std::uint64_t sample=0;
    const auto* end=value.data()+value.size();
    const auto parsed=std::from_chars(value.data(),end,sample);
    if(parsed.ec!=std::errc{} || parsed.ptr!=end)throw std::invalid_argument("Invalid sample index or count");
    return sample;
}

int run(const std::vector<std::string>& args) try {
    using namespace signalscope;
    if((args.size()!=7 && args.size()!=11) || args[1]!="--input" || args[3]!="--output" || args[5]!="--language" ||
       args[6].empty())
        throw std::invalid_argument("Expected --input METADATA --output REPORT --language TAG");
    const auto info=inspect_recording(path_from_utf8(args[2]));
    const std::uint64_t maximum=5ULL*info.rate;
    std::uint64_t first=0,count=std::min(info.pairs,maximum);
    if(args.size()==11) {
        if(args[7]!="--first-sample" || args[9]!="--samples")throw std::invalid_argument("Invalid range options");
        first=parseSample(args[8]);
        count=parseSample(args[10]);
        if(first>=info.pairs || !count || count>info.pairs-first || count>maximum)
            throw std::invalid_argument("Sample range must fit the recording and be at most five seconds");
    }
    PowerStats stats(info.format);
    replay(info,first,count,0,65536,[&](auto bytes,auto){stats.add(bytes);},[]{return false;});
    const auto result=stats.json();
    auto catalog=Json::parse(power_report_catalog);
    const auto external=application_directory()/"power-report-catalog.json";
    if(std::filesystem::is_regular_file(external)){std::ifstream input(external);catalog=Json::parse(input);}
    const auto locales=locale_fallbacks(args[6]);
    const auto label=[&](const char* key){
        Json translations=Json::object();for(const auto& [locale,labels]:catalog.items())if(labels.contains(key))translations[locale]=labels.at(key);
        return localized_value(translations,locales,key);
    };
    const auto output=path_from_utf8(args[4]);
    if(std::filesystem::exists(output))throw std::invalid_argument("Report output already exists");
    std::ofstream out(output,std::ios::binary);
    out.imbue(std::locale::classic());
    out.exceptions(std::ios::failbit|std::ios::badbit);
    out<<label("title")<<"\n\n"
       <<label("format")<<": "<<sample_datatype(info.format)<<'\n'
       <<label("rate")<<": "<<info.rate<<" Hz\n"
       <<label("frequency")<<": "<<std::fixed<<std::setprecision(6)<<info.frequency/1e6<<" MHz\n"
       <<label("first")<<": "<<first<<'\n'
       <<label("pairs")<<": "<<stats.pairs<<'\n'
       <<label("available")<<": "<<info.pairs<<'\n'
       <<label("duration")<<": "<<double(stats.pairs)/info.rate<<" s\n"
       <<label("power")<<": ";
    if(result["mean_power_dbfs"].is_number())out<<std::setprecision(2)<<result["mean_power_dbfs"].get<double>()<<" dBFS";
    else out<<label("missing");
    out<<'\n'<<label("clipped")<<": "<<stats.clipped_pairs<<'\n'
       <<label("dc")<<": "<<std::setprecision(6)<<result["dc_i"].get<double>()<<" / "<<result["dc_q"].get<double>()
       <<"\n\n"<<label("note")<<'\n';
    out.close();
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
}

#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    std::vector<std::string> args;
    for(int i=0;i<argc;++i)args.push_back(signalscope::path_to_utf8(argv[i]));
    return run(args);
}
#else
int main(int argc,char** argv){return run({argv,argv+argc});}
#endif
