#include "signalscope/adapters/receivers/plugin_api.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <bit>
#include <cstdlib>
#include <numbers>
#include <limits>
#include <string>
#include <vector>
using Json=nlohmann::json;
namespace {
void emit(void* context,AmcReceiverResult reply,const Json& result){const auto text=result.dump();reply(context,text.data(),text.size());}
Json capabilities() {
    return {{"schema_version",1},{"backend","test.receiver"},{"model","Independent test receiver"},{"sample_datatype","cf32_le"},
        {"frequency_ranges_hz",Json::array({{{"min",5000000},{"max",6000000000ULL},{"step",0}}})},
        {"sample_rate_ranges_hz",Json::array()},{"sample_rates_hz",{1000000,2000000,4000000}},{"recommended_rates_hz",{4000000}},
        {"filter_bandwidths_hz",{500000,1000000,2000000,4000000}},{"antennas",{"SIM"}},
        {"manual_gains_tenths_db",{0,100,200}},{"manual_gains_known",true},{"frequency_source","simulated"},{"rate_source","simulated"},
        {"limited_antenna",""},{"limited_antenna_max_hz",0},{"extensions",Json::object()},
        {"direct_sampling_supported",false},{"direct_sampling_ranges_hz",Json::array()}};
}
Json parameters(const Json& config) {
    const auto settings=config.value("settings",Json::object());const bool wide=settings.value("profile",std::string("wide"))=="wide";
    return Json::array({
        {{"key","auto_gain"},{"type","boolean"},{"enabled",true}},
        {{"key","gain_tenths_db"},{"type","enum"},{"label_key","test.gain.label"}},
        {{"key","profile"},{"type","enum"},{"extension",true},{"choices",{"eco","wide"}},{"default","wide"},
         {"choice_labels",{{"eco",{{"label_key","test.profile.eco"}}},{"wide",{{"label_key","test.profile.wide"}}}}},
         {"label_key","test.profile.label"}},
        {{"key","preamp"},{"type","boolean"},{"extension",true},{"default",false},{"enabled",wide},
         {"label_key","test.preamp.label"}},
        {{"key","calibration_ppm"},{"type","number"},{"extension",true},{"default",0},{"minimum",-20},{"maximum",20},{"unit","ppm"},
         {"label_key","test.calibration.label"}}
    });
}
Json plan(const Json& c) {
    const auto rate=c.at("sample_rate_hz").get<unsigned>();const auto settings=c.value("settings",Json::object());
    auto issues=Json::array();
    const auto issue=[&](const char* code,const char* field) {
        issues.push_back({{"code",code},{"field",field},{"scope","hardware"},{"blocking",true},{"values",{{"message_key",std::string("issue.")+code}}}});
    };
    if(rate!=1000000 && rate!=2000000 && rate!=4000000)issue("test_rate","sample_rate_hz");
    if(c.at("frequency_hz").get<double>()<5000000 || c.at("frequency_hz").get<double>()>6e9)issue("test_frequency","frequency_hz");
    const auto profile=settings.value("profile",std::string("wide"));
    if(profile!="wide" && profile!="eco")issue("test_profile","settings.profile");
    if(profile=="eco" && rate>1000000)issue("test_profile_rate","sample_rate_hz");
    if(profile=="eco" && settings.value("preamp",false))issue("test_preamp","settings.preamp");
    if(std::abs(settings.value("calibration_ppm",0.))>20)issue("test_ppm","settings.calibration_ppm");
    for(const auto& [key,value]:settings.items())if(key!="profile" && key!="preamp" && key!="calibration_ppm")issue("test_unknown_setting","settings");
    return {{"hardware_valid",issues.empty()},{"valid",issues.empty()},{"filter_bandwidth_hz",rate*.8},
        {"maximum_search_width_hz",rate},{"bytes_per_second",rate*4.},{"nominal_adc_bits",16},{"issues",issues}};
}
Json applied(const Json& c) {
    if(!plan(c).at("valid").get<bool>())throw std::runtime_error("Conflicting test receiver settings");
    return {{"requested",c},{"actual",{{"sample_rate_hz",c.at("sample_rate_hz")},{"frequency_hz",c.at("frequency_hz")},
        {"filter_bandwidth_hz",c.at("sample_rate_hz").get<double>()*.8},{"sample_datatype","cf32_le"},{"settings",c.value("settings",Json::object())}}},
        {"readback_source","simulated"}};
}
template<class F> int guard(F action,void* context,AmcReceiverResult reply) noexcept {
    try{emit(context,reply,action());return 0;}catch(const std::exception& e){emit(context,reply,{{"kind","invalid"},{"error",e.what()}});return 1;}
}
int request(const char* operation,const char* text,void* context,AmcReceiverResult reply) {
    return guard([&]()->Json {
        const auto j=Json::parse(text);const std::string op(operation);
        if(op=="devices")return {{"backend","test.receiver"},{"connection",j.value("settings",Json::object())},{"devices",Json::array({{{"index",0},{"name","Test receiver"},{"identification",{{"label","Independent test receiver"},{"serial","TEST-001"}}}}})}};
        if(op=="probe")return {{"hardware","Independent test receiver"},{"connection",j.value("settings",Json::object())},{"capabilities",capabilities()},{"antennas",{"SIM"}},{"gain_tenths_db",{0,100,200}}};
        const auto c=j.at("configuration");
        if(op=="parameters")return parameters(c);
        if(op=="plan")return plan(c);
        if(op=="configure")return applied(c);
        if(op=="recommend") {
            auto recommended=c;recommended["sample_rate_hz"]=2000000;recommended["auto_gain"]=true;
            recommended["settings"]={{"profile","wide"},{"preamp",false},{"calibration_ppm",0}};
            // Intentionally different from advertised presets: only the plugin knows this policy.
            return plan(recommended).at("valid").get<bool>()?Json::array({recommended}):Json::array();
        }
        throw std::runtime_error("Unknown operation");
    },context,reply);
}
int stream(const char* text,void* context,AmcReceiverSamples samples,AmcReceiverReady ready,AmcReceiverCancelled cancelled,AmcReceiverResult reply) {
    return guard([&] {
        const auto c=Json::parse(text);const auto a=applied(c);const auto rate=c.at("sample_rate_hz").get<unsigned>();
        const Json conditions={{"schema","org.amc.acquisition-conditions"},{"version",1},{"scope","before_first_samples"},{"origin","provider"},
            {"frequency_calibration",{{"state","calibrated"},{"evidence",{{"reference_id","independent-fixture.clock"},{"synthetic",true}}}}},
            {"amplitude_calibration",{{"state","not_applicable"},{"evidence",{{"source_kind","synthetic"}}}}},
            {"adc_overload",{{"capability","not_applicable"},{"state","not_applicable"},{"evidence",{{"source_kind","synthetic"}}}}},
            {"independent_future_condition",{{"value",42}}}};
        auto startup=Json({{"protocol_version",2},{"acquisition_conditions",conditions},{"sample_rate_hz",rate},{"frequency_hz",c.at("frequency_hz")},{"filter_bandwidth_hz",rate*.8},{"configuration",a}});
        const auto* startupFault=std::getenv("AMC_TEST_RECEIVER_FAULT");const std::string mode=startupFault?startupFault:"";
        if(mode=="legacy-readback"){startup.erase("protocol_version");startup.erase("acquisition_conditions");}
        if(mode=="readback-version")startup["protocol_version"]=99;
        if(mode=="readback-fraction")startup["protocol_version"]=2.5;
        if(mode=="readback-missing-conditions")startup.erase("acquisition_conditions");
        if(mode=="bad-conditions")startup["acquisition_conditions"]["adc_overload"]["state"]="not_detected";
        const auto response=startup.dump();
        if(ready(context,response.data(),response.size()))throw std::runtime_error("Host rejected startup");
        if(mode=="duplicate-ready" && ready(context,response.data(),response.size()))throw std::runtime_error("Host rejected repeated startup");
        const auto seconds=c.value("seconds",.1);
        const auto target=seconds>0?static_cast<std::uint64_t>(seconds*rate):std::numeric_limits<std::uint64_t>::max()/4;
        std::uint64_t pairs=0;double phase=0;
        const auto block=std::max(std::size_t(8),c.value("block_bytes",std::size_t(65536))/8*8);std::vector<std::uint8_t> bytes(block);
        while(pairs<target && !cancelled(context)) {
            const auto count=std::min<std::uint64_t>(block/8,target-pairs);
            for(std::uint64_t i=0;i<count;++i) {
                phase+=2*std::numbers::pi*(100000+(((pairs+i)/(rate/4000))%2?12000:-12000))/rate;
                const float values[]{static_cast<float>(12000./32768*std::cos(phase)),static_cast<float>(12000./32768*std::sin(phase))};
                for(unsigned component=0;component<2;++component) {
                    const auto word=std::bit_cast<std::uint32_t>(values[component]);
                    for(unsigned byte=0;byte<4;++byte)bytes[i*8+component*4+byte]=word>>(8*byte);
                }
            }
            const auto* fault=std::getenv("AMC_TEST_RECEIVER_FAULT");
            const auto first=pairs && fault && std::string(fault)=="gap"?pairs+1:pairs;
            if(samples(context,bytes.data(),count*8,first))throw std::runtime_error("Host stopped sample delivery");pairs+=count;
        }
        return Json{{"backend","test.receiver"},{"status",pairs==target?"complete":"cancelled"},{"accepted_pairs",pairs},{"rejected_pairs",0},{"power",{{"pairs",pairs}}},{"configuration",a}};
    },context,reply);
}
}
extern "C" SIGNALSCOPE_RECEIVER_EXPORT const AmcReceiverPluginV1* signalscope_receiver_plugin_v1() {
    static const auto metadata=[] {
        auto value=Json::parse(R"({"id":"test.receiver","name":"Independent test receiver","source":"live-test.receiver","recording_source":"test.receiver","format":"cf32_le","default_gain":"auto","priority":900,"catalogs":["amc-receiver-test.i18n"],"monitor_rates":[1000000,2000000,4000000],"request_protocol":2,"connection_parameters":[]})");
        const auto* fault=std::getenv("AMC_TEST_RECEIVER_FAULT");const std::string mode=fault?fault:"";
        if(mode=="request-version")value["request_protocol"]=99;
        if(mode=="request-fraction")value["request_protocol"]=2.5;
        if(mode=="request-missing-fields")value.erase("connection_parameters");
        if(mode=="legacy-request"){value.erase("request_protocol");value.erase("connection_parameters");}
        return value.dump();
    }();
    static const AmcReceiverPluginV1 api{1,sizeof(AmcReceiverPluginV1),metadata.c_str(),request,stream};return &api;
}
