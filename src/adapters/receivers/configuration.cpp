#include "signalscope/adapters/receivers/configuration.hpp"
#include <algorithm>
#include <cmath>

namespace signalscope {
bool ReceiverRange::contains(double value) const {
    return std::isfinite(value) && value>=minimum && value<=maximum &&
        (step<=0 || std::abs(std::remainder(value-minimum,step))<1e-6);
}
void to_json(Json& j,const ReceiverRange& r){j={{"min",r.minimum},{"max",r.maximum},{"step",r.step}};}
void from_json(const Json& j,ReceiverRange& r){r={j.at("min").get<double>(),j.at("max").get<double>(),j.value("step",0.)};}
void to_json(Json& j,const ReceiverCapabilities& c) {
    j={{"schema_version",1},{"backend",c.backend},{"model",c.model},{"sample_datatype",sample_datatype(c.format)},
       {"frequency_ranges_hz",c.frequency_ranges},{"sample_rate_ranges_hz",c.sample_rate_ranges},
       {"sample_rates_hz",c.sample_rates},{"recommended_rates_hz",c.recommended_rates},
       {"filter_bandwidths_hz",c.filter_bandwidths},{"antennas",c.antennas},
       {"manual_gains_tenths_db",c.manual_gains_tenths},{"manual_gains_known",c.manual_gains_known},
       {"frequency_source",c.frequency_source},{"rate_source",c.rate_source},
       {"limited_antenna",c.limited_antenna},{"limited_antenna_max_hz",c.limited_antenna_max_hz},
       {"extensions",c.extensions},{"direct_sampling_supported",c.direct_sampling_supported},
       {"direct_sampling_ranges_hz",c.direct_sampling_ranges},{"configuration_requires_stop",true}};
}
void from_json(const Json& j,ReceiverCapabilities& c) {
    if(j.at("schema_version")!=1)throw std::invalid_argument("Unsupported receiver capability schema");
    c.backend=j.at("backend");c.model=j.at("model");
    const auto format=j.at("sample_datatype").get<std::string>();
    c.format=sample_format_from_name(format);
    j.at("frequency_ranges_hz").get_to(c.frequency_ranges);j.at("sample_rate_ranges_hz").get_to(c.sample_rate_ranges);
    j.at("sample_rates_hz").get_to(c.sample_rates);j.at("recommended_rates_hz").get_to(c.recommended_rates);
    j.at("filter_bandwidths_hz").get_to(c.filter_bandwidths);j.at("antennas").get_to(c.antennas);
    j.at("manual_gains_tenths_db").get_to(c.manual_gains_tenths);c.manual_gains_known=j.at("manual_gains_known");
    c.frequency_source=j.at("frequency_source");c.rate_source=j.at("rate_source");
    c.limited_antenna=j.at("limited_antenna");c.limited_antenna_max_hz=j.at("limited_antenna_max_hz");
    c.extensions=j.value("extensions",Json::object());
    c.direct_sampling_supported=j.value("direct_sampling_supported",false);
    c.direct_sampling_ranges=j.value("direct_sampling_ranges_hz",std::vector<ReceiverRange>{});
}
void to_json(Json& j,const ConfigurationIssue& i) {
    j={{"code",i.code},{"field",i.field},{"scope",i.scope},{"blocking",i.blocking},{"values",i.values}};
}
bool ReceiverPlan::hardwareValid() const {
    return std::none_of(issues.begin(),issues.end(),[](const auto& i){return i.blocking && i.scope=="hardware";});
}
bool ReceiverPlan::valid() const {
    return std::none_of(issues.begin(),issues.end(),[](const auto& i){return i.blocking;});
}
void to_json(Json& j,const ReceiverPlan& p) {
    j={{"effective_configuration",p.effective_configuration},{"hardware_valid",p.hardwareValid()},{"valid",p.valid()},{"filter_bandwidth_hz",p.filter_bandwidth_hz},
       {"maximum_search_width_hz",p.maximum_search_width_hz},{"bytes_per_second",p.bytes_per_second},
       {"nominal_adc_bits",p.nominal_adc_bits?Json(*p.nominal_adc_bits):Json()},
       {"adc_bits_source",p.nominal_adc_bits?"hardware_specification":"unknown"},
       {"lora_analysis_rate_hz",p.lora_analysis_rate_hz},{"lora_channels",p.lora_channels},
       {"complete_signal_search_width_hz",p.complete_signal_search_width_hz},{"issues",p.issues}};
}
void from_json(const Json& j,ConfigurationIssue& i) {
    i={j.at("code"),j.at("field"),j.at("scope"),j.at("blocking"),j.value("values",Json::object())};
}
void from_json(const Json& j,ReceiverPlan& p) {
    p.effective_configuration=j.value("effective_configuration",Json::object());
    p.filter_bandwidth_hz=j.at("filter_bandwidth_hz");p.maximum_search_width_hz=j.at("maximum_search_width_hz");
    p.bytes_per_second=j.at("bytes_per_second");p.lora_analysis_rate_hz=j.value("lora_analysis_rate_hz",0u);
    p.lora_channels=j.value("lora_channels",std::size_t{});p.complete_signal_search_width_hz=j.value("complete_signal_search_width_hz",0.);
    if(!j.at("nominal_adc_bits").is_null())p.nominal_adc_bits=j.at("nominal_adc_bits").get<unsigned>();
    j.at("issues").get_to(p.issues);
}
void requireReceiverConfiguration(const ReceiverPlan& p) {
    if(p.hardwareValid())return;
    for(const auto& i:p.issues)if(i.blocking && i.scope=="hardware") {
        const auto code=i.code=="receiver_passband"?ErrorCode::receiver_passband:
            i.code=="antenna_frequency"?ErrorCode::receiver_antenna_frequency:ErrorCode::receiver_configuration;
        throw OperationError(code,"Invalid receiver configuration: "+i.code+" ("+i.field+")");
    }
}
Json requestedReceiverConfiguration(const Config& c) {
    return {{"sample_rate_hz",c.rate},{"frequency_hz",c.frequency},{"antenna",c.antenna},
            {"auto_gain",c.auto_gain},{"gain_tenths_db",c.auto_gain?Json():Json(c.gain_tenths)},
            {"input_mode",c.input_mode},{"if_bandwidth_hz",c.if_bandwidth_hz},
            {"settings",c.receiver_settings},{"device",c.device},{"serial",c.device_serial},{"required_passband_hz",c.required_passband_hz}};
}
}
