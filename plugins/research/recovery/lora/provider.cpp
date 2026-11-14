#include "provider_config.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace signalscope::recovery::lora {
namespace {
Json number(const char* key,double value,double low,double high){return {{"key",key},{"type","number"},{"extension",true},{"label_key",key},{"default",value},{"minimum",low},{"maximum",high}};}
Json integer(const char* key,unsigned value,unsigned low,unsigned high){auto field=number(key,value,low,high);field["type"]="integer";field["default"]=value;return field;}
Json boolean(const char* key,bool value){return {{"key",key},{"type","boolean"},{"extension",true},{"label_key",key},{"default",value}};}
unsigned pendingUnsigned(const Json& values,const char* key,unsigned fallback){if(!values.contains(key) || !values.at(key).is_number_integer() || values.at(key)<0 || values.at(key)>UINT32_MAX)return fallback;return values.at(key).get<unsigned>();}
Json describe(const RecoveryContext& context,const Json& pending) {
    if(!context.analysis)throw std::invalid_argument("LoRa parameters need an actual input clock");
    const auto rate=context.analysis->sample_rate(),limit=static_cast<unsigned>(std::min<std::uint64_t>(context.range.count?context.range.count-1:0,999999));const auto data=pendingUnsigned(pending,"data_offset_samples",0),timingStep=pendingUnsigned(pending,"timing_step_samples",1);const auto reference=pending.contains("use_reference") && pending.at("use_reference").is_boolean() && pending.at("use_reference").get<bool>();
    auto fields=Json::array({integer("sf",7,7,12),integer("bandwidth_hz",std::min(125000u,rate),1,rate),integer("data_offset_samples",0,0,limit),boolean("use_reference",false),integer("reference_offset_samples",0,0,reference?std::min(limit,data?data-1:0):limit),boolean("inverted_iq",false),number("carrier_offset_hz",0,-double(rate)/2,rate/2),integer("maximum_symbols",64,8,1024),
        {{"key","fft_padding"},{"type","enum"},{"extension",true},{"label_key","fft_padding"},{"default",4},{"choices",Json::array({1,2,4,8})}},
        integer("timing_step_samples",1,1,65536),integer("timing_radius_samples",0,0,static_cast<unsigned>(std::min<std::uint64_t>(65536,std::uint64_t(timingStep)*4))),number("minimum_rms",.0001,0,10),number("minimum_concentration",.02,0,1),
        number("drift_min",0,-1,1),number("drift_max",0,-1,1),number("drift_step",.002,.000001,2),number("bias_min",0,-4,4),number("bias_max",0,-4,4),number("bias_step",.125,.000001,8),
        boolean("explicit_header",true),boolean("low_data_rate",false),boolean("correct_single_bit",true),integer("payload_bytes",50,0,255),integer("coding_rate",1,1,4),boolean("payload_crc",false)});
    for(auto& field:fields){const auto key=field.at("key").get<std::string>();if(key=="reference_offset_samples"){field["enabled_when"]={{"key","use_reference"},{"equals",true}};field["inactive_allowed"]=true;}if(key=="payload_bytes" || key=="coding_rate" || key=="payload_crc"){field["enabled_when"]={{"key","explicit_header"},{"equals",false}};field["inactive_allowed"]=true;}}
    return fields;
}
PhyConfiguration parse(const RecoveryContext& context,const Json& supplied) {
    PhyConfiguration parsed;auto& errors=parsed.failure;auto& values=parsed.parameters;const auto fields=describe(context,supplied);const auto issue=[&](const char* field,const char* key){errors.issues.push_back({{"field",field},{"label_key",key}});};
    for(const auto& field:fields){const auto key=field.at("key").get<std::string>(),type=field.at("type").get<std::string>();const auto value=supplied.value(key,field.at("default"));values[key]=value;const auto valid=type=="boolean"?value.is_boolean():type=="enum"?std::find(field.at("choices").begin(),field.at("choices").end(),value)!=field.at("choices").end():value.is_number() && (type!="integer" || value.is_number_integer()) && std::isfinite(value.get<double>()) && value>=field.at("minimum") && value<=field.at("maximum");if(!valid)issue(key.c_str(),"issue.parameter");}
    for(const auto& [key,value]:supplied.items()){(void)value;if(!values.contains(key))issue(key.c_str(),"issue.unknown");}if(!errors.issues.empty())return parsed;
    auto& options=parsed.options;options.packet.sf=values.at("sf");options.bandwidth_hz=values.at("bandwidth_hz");options.data_offset_samples=values.at("data_offset_samples");options.reference_offset_samples=values.at("reference_offset_samples");options.use_reference=values.at("use_reference");options.inverted_iq=values.at("inverted_iq");options.carrier_offset_hz=values.at("carrier_offset_hz");options.maximum_symbols=values.at("maximum_symbols");options.fft_padding=values.at("fft_padding");options.timing_step_samples=values.at("timing_step_samples");options.timing_radius_samples=values.at("timing_radius_samples");options.minimum_rms=values.at("minimum_rms");options.minimum_concentration=values.at("minimum_concentration");options.drift_min=values.at("drift_min");options.drift_max=values.at("drift_max");options.drift_step=values.at("drift_step");options.bias_min=values.at("bias_min");options.bias_max=values.at("bias_max");options.bias_step=values.at("bias_step");options.packet.explicit_header=values.at("explicit_header");options.packet.low_data_rate=values.at("low_data_rate");options.packet.correct_single_bit=values.at("correct_single_bit");options.packet.payload_bytes=values.at("payload_bytes");options.packet.coding_rate=values.at("coding_rate");options.packet.payload_crc=values.at("payload_crc");
    const auto rate=context.analysis->sample_rate();if(rate%options.bandwidth_hz || !std::has_single_bit(rate/options.bandwidth_hz) || rate/options.bandwidth_hz>16)issue("bandwidth_hz","issue.clock");
    const auto grid=[&](double low,double high,double step){const auto count=(high-low)/step;return low<=high && count<=32768 && std::abs(count-std::round(count))<1e-7;};if(!grid(options.drift_min,options.drift_max,options.drift_step))issue("drift_step","issue.grid");if(!grid(options.bias_min,options.bias_max,options.bias_step))issue("bias_step","issue.grid");if(options.timing_radius_samples%options.timing_step_samples)issue("timing_radius_samples","issue.grid");if(options.use_reference && options.reference_offset_samples>=options.data_offset_samples)issue("reference_offset_samples","issue.reference");if(!errors.issues.empty())return parsed;
    return parsed;
}
}
Json phy_descriptions(const RecoveryContext& context,const Json& supplied){return describe(context,supplied);}
PhyConfiguration parse_phy(const RecoveryContext& context,const Json& supplied){return parse(context,supplied);}
RecoveryData recover_phy(const RecoveryContext& context,const Json& supplied,std::stop_token stop){auto config=parse(context,supplied);if(!config.failure.issues.empty())return config.failure;auto result=demodulation_evidence(context,demodulate(context,config.options,stop),stop);result.parameters=std::move(config.parameters);return result;}
}
namespace signalscope::recovery {
RecoveryProvider loraPhy(){return {"bounded-lora-phy","recovery-lora","label","about",1,1,lora::phy_descriptions,lora::recover_phy};}
}
