#pragma once
#include "signalscope/backend/dsp/reference_model.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace signalscope::models {
inline Json number(const char* key,double value,double minimum,double maximum,const char* unit="") {
    return {{"key",key},{"type","number"},{"extension",true},{"label_key",key},{"default",value},{"minimum",minimum},{"maximum",maximum},{"unit",unit}};
}
inline Json integer(const char* key,unsigned value,unsigned minimum,unsigned maximum) {
    auto result=number(key,value,minimum,maximum);result["type"]="integer";result["default"]=value;result["minimum"]=minimum;result["maximum"]=maximum;return result;
}
inline Json common(const ReferenceModelContext& context) {
    return Json::array({number("amplitude",1,0,10),number("phase_rad",0,-1e9,1e9,"rad"),number("carrier_hz",0,-double(context.sample_rate)/2.,context.sample_rate/2.,"Hz")});
}
inline void issue(PreparedReferenceModel& model,const char* field,const char* key){model.issues.push_back({{"field",field},{"label_key",key}});}
inline PreparedReferenceModel values(const Json& descriptors,const Json& supplied) {
    PreparedReferenceModel model;std::set<std::string> fields;
    for(const auto& descriptor:descriptors) {
        const auto key=descriptor.at("key").get<std::string>(),type=descriptor.at("type").get<std::string>();fields.insert(key);
        const auto value=supplied.value(key,descriptor.at("default"));model.parameters[key]=value;bool valid=true;
        if(type=="number" || type=="integer")valid=value.is_number() && (type!="integer" || value.is_number_integer()) && std::isfinite(value.get<double>()) && value>=descriptor.at("minimum") && value<=descriptor.at("maximum");
        else if(type=="enum")valid=std::find(descriptor.at("choices").begin(),descriptor.at("choices").end(),value)!=descriptor.at("choices").end();
        else valid=value.is_string();
        if(!valid)issue(model,key.c_str(),"issue.parameter");
    }
    for(const auto& [key,value]:supplied.items()){(void)value;if(!fields.contains(key))issue(model,key.c_str(),"issue.unknown");}
    return model;
}
inline void band(PreparedReferenceModel& model,const ReferenceModelContext& context,double low,double high,const char* field,bool closed=false) {
    const auto half=context.sample_rate/2.;
    if(!std::isfinite(low) || !std::isfinite(high) || (closed?(low< -half || high>half):(low<= -half || high>=half)))issue(model,field,"issue.nyquist");
}
inline std::complex<double> oscillator(double amplitude,double phase,long double cycles) {
    const auto angle=static_cast<double>(2*std::numbers::pi_v<long double>*std::remainder(cycles,1.L)+std::remainder(static_cast<long double>(phase),2*std::numbers::pi_v<long double>));
    return amplitude*std::polar(1.,angle);
}
template<class F>void generator(PreparedReferenceModel& model,const ReferenceModelContext& context,F function,std::size_t extra=0) {
    if(extra>context.workspace_payload_bytes || sizeof(F)>context.workspace_payload_bytes-extra)throw std::length_error("Reference generator exceeds its workspace budget");
    model.workspace_payload_bytes=sizeof(F)+extra;model.evaluate=std::move(function);
}
inline ReferenceModelProvider provider(std::string id,std::string resources,
    std::function<Json(const ReferenceModelContext&,const Json&)> describe,
    std::function<PreparedReferenceModel(const ReferenceModelContext&,const Json&)> prepare,unsigned protocol=2) {
    ReferenceModelProvider value;value.id=std::move(id);value.resource_namespace=std::move(resources);value.label_key="label";value.description_key="about";value.protocol_version=protocol;value.describe=std::move(describe);value.prepare=std::move(prepare);return value;
}
ReferenceModelProvider tone();ReferenceModelProvider am();ReferenceModelProvider fm();
ReferenceModelProvider fsk();ReferenceModelProvider chirp();ReferenceModelProvider lora();
ReferenceModelProvider psk();ReferenceModelProvider ofdm();
}
