#include "signalscope/adapters/television/dtmb_receiver.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <cmath>
#include <array>
#include <thread>

namespace signalscope::television {
namespace {
const TextCatalog& catalog(){static const auto value=TextCatalog::resource("dtmb-receiver");return value;}
Json field(const char* key,const char* type,const char* label) {
    return {{"key",key},{"type",type},{"label_key",label},{"extension",true}};
}
}
void validateDtmbReceiveOptions(const DtmbReceiveOptions& options) {
    if(options.input.sample_rate<7560000 || options.input.sample_rate>100000000 ||
       !std::isfinite(options.input.center_frequency_hz) || !std::isfinite(options.frequency_hz) ||
       options.workers<1 || options.workers>64 || !options.maximum_iterations || options.maximum_iterations>200)
        throw std::invalid_argument("dtmb.invalid_input");
    sample_codec(options.input.format);
    if(options.pn_symbols && options.pn_symbols!=420 && options.pn_symbols!=595 && options.pn_symbols!=945)
        throw std::invalid_argument("dtmb.invalid_pn");
    if(options.system_info && (options.system_info<3 || options.system_info>24))throw std::invalid_argument("dtmb.invalid_si");
    if(options.frame_body!="auto" && options.frame_body!="c1" && options.frame_body!="c3780")throw std::invalid_argument("dtmb.invalid_body");
    if(options.frame_body=="c1" && ((options.pn_symbols && options.pn_symbols!=595) ||
        (options.system_info && (options.system_info<5 || options.system_info>10))))throw std::invalid_argument("dtmb.unsupported_c1_profile");
    const auto offset=options.frequency_hz?options.frequency_hz-options.input.center_frequency_hz:0;
    if(std::abs(offset)+3780000>options.input.sample_rate/2.)throw std::invalid_argument("dtmb.outside_input");
    const auto target=static_cast<std::size_t>(std::ceil(options.input.sample_rate*.30))*8;
    if(options.probe_bytes>512*1024*1024 || options.probe_bytes<target)throw std::invalid_argument("dtmb.probe_budget");
}
Json dtmbRecommendedParameters() {
    return {{"pn_symbols",0},{"system_info",0},{"frame_body","auto"},
        {"workers",std::clamp(std::thread::hardware_concurrency()/2,1u,16u)},{"maximum_iterations",100}};
}
Json dtmbReceiveParameters(const Json& pending) {
    const auto defaults=dtmbRecommendedParameters();
    auto pn=field("pn_symbols","enum","parameter.pn"),si=field("system_info","enum","parameter.si"),
        body=field("frame_body","enum","parameter.body"),workers=field("workers","integer","parameter.workers"),
        iterations=field("maximum_iterations","integer","parameter.iterations");
    const auto selected=pending.find("frame_body");const auto single=selected!=pending.end() && selected->is_string() && *selected=="c1";
    pn["choices"]=single?Json::array({0,595}):Json::array({0,420,595,945});
    pn["choice_labels"]={{"0",{{"label_key","parameter.auto"}}}};
    for(const auto count:{420,595,945})pn["choice_labels"][std::to_string(count)]={{"label_key","parameter.pn_value"},{"arguments",{{"symbols",count}}}};
    si["choices"]=Json::array({0});si["choice_labels"]={{"0",{{"label_key","parameter.auto"}}}};
    for(unsigned profile=3;profile<=24;++profile) {
        if(single && (profile<5 || profile>10))continue;
        si["choices"].push_back(profile);const auto mode=dtmbMode(945,profile);
        constexpr std::array<double,4> rates{0,.4,.6,.8};
        si["choice_labels"][std::to_string(profile)]={{"label_key","parameter.profile"},
            {"arguments",{{"id",profile},{"qam",mode.qam},{"rate",rates[mode.fec_rate]},{"interleaver",profile%2?240:720}}}};
    }
    body["choices"]=Json::array({"auto","c3780","c1"});
    for(const auto* value:{"auto","c3780","c1"})body["choice_labels"][value]={{"label_key",std::string("parameter.body.")+value}};
    body["description_key"]="parameter.body_hint";
    workers.update({{"minimum",1},{"maximum",64},{"description_key","parameter.workers_hint"}});
    iterations.update({{"minimum",1},{"maximum",200}});
    Json result=Json::array({pn,si,body,workers,iterations});
    for(auto& item:result)item["default"]=defaults.at(item.at("key").get<std::string>());
    catalog().resolve(result);return result;
}
DtmbReceiveOptions dtmbReceiveOptions(IqStreamParameters input,double frequency,const Json& pending) {
    try {
        auto values=dtmbRecommendedParameters();values.update(pending);DtmbReceiveOptions options{input};options.frequency_hz=frequency;
        options.pn_symbols=values.at("pn_symbols").get<unsigned>();options.system_info=values.at("system_info").get<unsigned>();
        options.frame_body=values.at("frame_body").get<std::string>();options.workers=values.at("workers").get<unsigned>();
        options.maximum_iterations=values.at("maximum_iterations").get<unsigned>();
        options.probe_bytes=std::max(options.probe_bytes,static_cast<std::size_t>(std::ceil(input.sample_rate*.30))*8);
        validateDtmbReceiveOptions(options);return options;
    }catch(const Json::exception&){throw std::invalid_argument("dtmb.invalid_settings");}
}
Json dtmbReceiveDiagnostic(const std::string& key,const Json& args) {
    Json node={{"message_key",key},{"arguments",args}};catalog().resolve(node);return node;
}
}
