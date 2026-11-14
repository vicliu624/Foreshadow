#include "signalscope/adapters/receivers/plugin_support.hpp"
#include <cmath>

namespace signalscope::receiver_plugin {
Json describeParameters(const ReceiverCapabilities& caps,const Config& cfg) {
    const bool direct=receiverInputMode(caps,cfg)!="tuner";
    const auto field=[](const char* key,const char* type) {
        return Json{{"key",key},{"type",type},{"label_key",std::string("parameter.")+key+".label"},
            {"enabled",true},{"requires_stop",true}};
    };
    auto fields=Json::array();
    auto rate=field("sample_rate_hz","integer");
    rate["unit"]="S/s";rate["choices"]=receiverRateChoices(caps);rate["ranges"]=caps.sample_rate_ranges;fields.push_back(rate);
    auto freq=field("frequency_hz","number");freq["unit"]="Hz";
    freq["ranges"]=direct?caps.direct_sampling_ranges:caps.frequency_ranges;fields.push_back(freq);
    auto gain=field("gain_tenths_db","enum");gain["enabled"]=!direct && !cfg.auto_gain;
    gain["choices"]=caps.manual_gains_tenths;gain["unit"]="0.1 dB";
    if(caps.extensions.value("gain_semantic",std::string())=="if_reduction")gain["label_key"]="parameter.if_gain_reduction.label";
    fields.push_back(gain);
    auto agc=field("auto_gain","boolean");agc["enabled"]=!direct;fields.push_back(agc);
    auto input=field("input_mode","enum");
    input["choices"]=Json::array({"auto","tuner"});
    if(caps.direct_sampling_supported){input["choices"].push_back("direct_i");input["choices"].push_back("direct_q");}
    input["visible"]=caps.direct_sampling_supported;fields.push_back(input);
    auto bw=field("if_bandwidth_hz","enum");
    bw["choices"]=caps.filter_bandwidths;bw["choices"].insert(bw["choices"].begin(),0.);bw["visible"]=!caps.filter_bandwidths.empty();fields.push_back(bw);
    auto antenna=field("antenna","enum");antenna["choices"]=caps.antennas;fields.push_back(antenna);
    for(auto extra:caps.extensions.value("settings",Json::array())) {
        extra["extension"]=true;
        if(extra.contains("enabled_frequency_max_hz"))extra["enabled"]=cfg.frequency<=extra.at("enabled_frequency_max_hz").get<double>();
        if(extra.value("requires_auto_gain",false))extra["enabled"]=cfg.auto_gain;
        if(extra.contains("enabled_when")) {
            const auto& rule=extra.at("enabled_when");
            extra["enabled"]=cfg.receiver_settings.value(rule.at("key").get<std::string>(),Json())==rule.at("equals");
        }
        fields.push_back(std::move(extra));
    }
    return fields;
}
std::vector<Config> recommendHardware(const ReceiverCapabilities& caps,const Config& requested) {
    auto cfg=requested;cfg.auto_gain=true;cfg.input_mode="auto";cfg.if_bandwidth_hz=0;
    const auto parameters=describeParameters(caps,cfg);
    const auto current=validateHardware(caps,cfg);
    for(const auto& issue:current.issues)if(issue.blocking && issue.field.starts_with("settings.")) {
        const auto key=issue.field.substr(9);
        const auto descriptor=std::find_if(parameters.begin(),parameters.end(),[&](const auto& d){return d.value("extension",false) && d.value("key",std::string())==key;});
        if(descriptor!=parameters.end() && descriptor->contains("default"))cfg.receiver_settings[key]=descriptor->at("default");
        else cfg.receiver_settings.erase(key);
    }
    const auto mode=receiverInputMode(caps,cfg);
    const auto& frequencies=mode=="tuner"?caps.frequency_ranges:caps.direct_sampling_ranges;
    if(!frequencies.empty() && std::none_of(frequencies.begin(),frequencies.end(),
        [&](const auto& r){return r.contains(cfg.frequency);}))return {};
    const auto valid=[&](const std::string& antenna) {
        return (antenna.empty() || std::find(caps.antennas.begin(),caps.antennas.end(),antenna)!=caps.antennas.end()) &&
            (antenna.empty() || antenna!=caps.limited_antenna || cfg.frequency<=caps.limited_antenna_max_hz);
    };
    if(!valid(cfg.antenna)) {
        const auto port=std::find_if(caps.antennas.begin(),caps.antennas.end(),valid);
        if(port==caps.antennas.end())return {};
        cfg.antenna=*port;
    }
    // A plugin may override this policy; the host only checks analysis needs.
    auto rates=caps.recommended_rates;auto other=receiverRateChoices(caps);
    std::reverse(other.begin(),other.end());rates.insert(rates.end(),other.begin(),other.end());
    std::vector<Config> result;
    for(const auto rate:rates){cfg.rate=rate;if(validateHardware(caps,cfg).valid())result.push_back(cfg);}
    return result;
}
namespace {
void emit(AmcReceiverResult callback,void* context,const Json& value) {
    const auto text=value.dump();callback(context,text.data(),text.size());
}
template<class F> int guarded(F&& action,void* context,AmcReceiverResult result) noexcept {
    try {emit(result,context,action());return 0;}
    catch(const SourceError& e){emit(result,context,{{"error",e.what()},{"kind","source"},{"report",e.details}});}
    catch(const OperationError& e){emit(result,context,{{"error",e.what()},{"kind","operation"},{"code",static_cast<int>(e.code())}});}
    catch(const std::exception& e){emit(result,context,{{"error",e.what()},{"kind","invalid"}});}
    catch(...){emit(result,context,{{"error","Unknown receiver plugin failure"},{"kind","operation"},{"code",static_cast<int>(ErrorCode::receiver_stream)}});}
    return 1;
}
}
int request(const ReceiverBackend& backend,const char* operation,const char* payload,void* ctx,AmcReceiverResult reply) noexcept {
    return guarded([&]()->Json {
        const auto j=Json::parse(payload);const std::string_view op(operation);
        if(op=="devices")return receiverDevices(backend,j.value("settings",Json::object()));
        if(op=="probe")return receiverProbe(backend,j.value("device",0u),j.value("serial",std::string()),j.value("settings",Json::object()));
        const auto cfg=decodeConfig(j.at("configuration"));
        if(op=="configure")return backend.configure(cfg);
        const auto caps=j.at("capabilities").get<ReceiverCapabilities>();
        if(op=="plan")return backend.plan(caps,cfg);
        if(op=="parameters")return backend.parameters(caps,cfg);
        if(op=="recommend") {
            auto candidates=Json::array();for(const auto& c:backend.recommend(caps,cfg))candidates.push_back(encodeConfig(c));
            return candidates;
        }
        throw std::invalid_argument("Unknown receiver plugin operation");
    },ctx,reply);
}
int stream(const ReceiverBackend& backend,const char* config,void* ctx,AmcReceiverSamples samples,
           AmcReceiverReady ready,AmcReceiverCancelled cancelled,AmcReceiverResult reply) noexcept {
    return guarded([&] {
        return backend.stream(decodeConfig(Json::parse(config)),[&](auto bytes,auto first) {
            if(samples(ctx,bytes.data(),bytes.size(),first))throw std::runtime_error("Host stopped receiver sample delivery");
        },[&]{return cancelled(ctx)!=0;},[&](const ReceiverReadback& actual) {
            const auto text=Json({{"sample_rate_hz",actual.sample_rate_hz},{"frequency_hz",actual.center_frequency_hz},
                {"filter_bandwidth_hz",actual.filter_bandwidth_hz},{"configuration",actual.configuration},
                {"protocol_version",receiverReadbackProtocolVersion},{"acquisition_conditions",actual.conditions}}).dump();
            if(ready(ctx,text.data(),text.size()))throw std::runtime_error("Host rejected receiver startup");
        });
    },ctx,reply);
}
}
