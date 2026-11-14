#include "signalscope/adapters/receivers/plugin_support.hpp"
#include <cmath>
namespace signalscope {
std::vector<std::uint32_t> receiverRateChoices(const ReceiverCapabilities& c) {
    auto rates=c.sample_rates;
    const auto add=[&](std::uint32_t rate){
        if(std::any_of(c.sample_rate_ranges.begin(),c.sample_rate_ranges.end(),[&](const auto& r){return r.contains(rate);}))rates.push_back(rate);
    };
    for(const auto rate:c.recommended_rates)add(rate);
    for(const auto rate:{250000u,1000000u,1750000u,1875000u,2000000u,2048000u,2400000u,
                         3000000u,4000000u,5000000u,6000000u,7000000u,8000000u,9000000u,10000000u})add(rate);
    std::sort(rates.begin(),rates.end());rates.erase(std::unique(rates.begin(),rates.end()),rates.end());return rates;
}
std::string receiverInputMode(const ReceiverCapabilities& caps,const Config& cfg) {
    if(cfg.input_mode!="auto")return cfg.input_mode;
    const auto contains=[&](const auto& ranges){return std::any_of(ranges.begin(),ranges.end(),[&](const auto& r){return r.contains(cfg.frequency);});};
    return caps.direct_sampling_supported && !contains(caps.frequency_ranges) && contains(caps.direct_sampling_ranges)?"direct_q":"tuner";
}
}
namespace signalscope::receiver_plugin {
ReceiverPlan validateHardware(const ReceiverCapabilities& c,const Config& cfg) {
    ReceiverPlan p;p.bytes_per_second=double(cfg.rate)*bytes_per_pair(c.format);
    const auto issue=[&](const char* code,const char* field,Json values=Json::object()){
        p.issues.push_back({code,field,"hardware",true,std::move(values)});
    };
    const auto contains=[](const auto& ranges,double value){
        return std::any_of(ranges.begin(),ranges.end(),[&](const auto& range){return range.contains(value);});
    };
    const bool listed=std::find(c.sample_rates.begin(),c.sample_rates.end(),cfg.rate)!=c.sample_rates.end();
    if(!cfg.rate || (!listed && !contains(c.sample_rate_ranges,cfg.rate)))issue("receiver_rate","sample_rate_hz");
    const auto mode=receiverInputMode(c,cfg);
    p.effective_configuration={{"input_mode",mode}};
    const bool direct=mode=="direct_i" || mode=="direct_q";
    if((mode!="tuner" && !direct) || (direct && !c.direct_sampling_supported))issue("receiver_input_mode","input_mode");
    const auto& frequencies=direct?c.direct_sampling_ranges:c.frequency_ranges;
    if(!frequencies.empty() && !contains(frequencies,cfg.frequency))issue("receiver_frequency","frequency_hz");
    if(frequencies.empty())p.issues.push_back({"frequency_unknown","frequency_hz","information",false});
    if(!cfg.antenna.empty() && std::find(c.antennas.begin(),c.antennas.end(),cfg.antenna)==c.antennas.end())
        issue("receiver_antenna","antenna");
    if(!cfg.antenna.empty() && cfg.antenna==c.limited_antenna && cfg.frequency>c.limited_antenna_max_hz)
        issue("antenna_frequency","frequency_hz",{{"maximum_hz",c.limited_antenna_max_hz}});
    if(!direct && !cfg.auto_gain && c.manual_gains_known &&
       std::find(c.manual_gains_tenths.begin(),c.manual_gains_tenths.end(),cfg.gain_tenths)==c.manual_gains_tenths.end())
        issue("receiver_gain","gain_tenths_db");
    if(cfg.if_bandwidth_hz>0) {
        if(std::find(c.filter_bandwidths.begin(),c.filter_bandwidths.end(),cfg.if_bandwidth_hz)==c.filter_bandwidths.end())issue("receiver_if_bandwidth","if_bandwidth_hz");
        else p.filter_bandwidth_hz=cfg.if_bandwidth_hz;
    } else {
        for(const auto bw:c.filter_bandwidths)
            if(bw<=cfg.rate && bw>p.filter_bandwidth_hz)p.filter_bandwidth_hz=bw;
        if(!p.filter_bandwidth_hz && !c.filter_bandwidths.empty())p.filter_bandwidth_hz=*std::min_element(c.filter_bandwidths.begin(),c.filter_bandwidths.end());
    }
    if(cfg.if_bandwidth_hz>cfg.rate)p.issues.push_back({"filter_above_sample_span","if_bandwidth_hz","information",false});
    p.maximum_search_width_hz=cfg.rate;
    if(!std::isfinite(cfg.if_bandwidth_hz) || cfg.if_bandwidth_hz<0)issue("receiver_if_bandwidth","if_bandwidth_hz");
    if(cfg.required_passband_hz>p.maximum_search_width_hz)
        issue("receiver_passband","search_width_hz",{{"required_hz",cfg.required_passband_hz},{"available_hz",p.maximum_search_width_hz}});
    if(!cfg.receiver_settings.is_object())issue("plugin_settings","settings");
    else {
        const auto parameters=describeParameters(c,cfg);
        for(const auto& [key,value]:cfg.receiver_settings.items()) {
            const auto d=std::find_if(parameters.begin(),parameters.end(),[&](const auto& item){return item.value("extension",false) && item.value("key",std::string())==key;});
            bool valid=d!=parameters.end();
            if(valid) {
                const auto type=d->value("type",std::string("string"));
                valid=type=="boolean"?value.is_boolean():type=="integer"?value.is_number_integer():
                    type=="number"?value.is_number():type=="enum"?std::find(d->at("choices").begin(),d->at("choices").end(),value)!=d->at("choices").end():value.is_string();
                if(valid && value.is_number())valid=std::isfinite(value.get<double>()) && value.get<double>()>=d->value("minimum",-1e300) && value.get<double>()<=d->value("maximum",1e300);
                if(valid && !d->value("enabled",true) && !d->value("inactive_allowed",false))valid=value==d->value("default",Json());
            }
            if(!valid)issue("plugin_setting",("settings."+key).c_str(),{{"message_key","issue.plugin_setting"},{"key",key}});
        }
    }
    return p;
}
}
