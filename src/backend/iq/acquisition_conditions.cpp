#include "signalscope/backend/iq/acquisition_conditions.hpp"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace signalscope {
namespace {
bool oneOf(const Json& value,std::initializer_list<std::string_view> choices) {
    return value.is_string() && std::any_of(choices.begin(),choices.end(),[&](auto choice){return value.get_ref<const std::string&>()==choice;});
}
void require(bool condition){if(!condition)throw std::invalid_argument("Invalid acquisition conditions report");}
void condition(const Json& value,std::initializer_list<std::string_view> states) {
    require(value.is_object() && value.contains("state") && oneOf(value.at("state"),states) && value.contains("evidence") && value.at("evidence").is_object());
}
}
AcquisitionConditions::AcquisitionConditions():report_{{"schema","org.amc.acquisition-conditions"},{"version",acquisitionConditionsProtocolVersion},
    {"scope","before_first_samples"},{"origin","not_reported"},
    {"frequency_calibration",{{"state","unknown"},{"evidence",Json::object()}}},
    {"amplitude_calibration",{{"state","unknown"},{"evidence",Json::object()}}},
    {"adc_overload",{{"capability","unknown"},{"state","unknown"},{"evidence",Json::object()}}}} {}
AcquisitionConditions::AcquisitionConditions(Json report):report_(std::move(report)) {
    require(report_.is_object() && report_.value("schema",std::string())=="org.amc.acquisition-conditions" &&
        report_.contains("version") && report_.at("version").is_number_integer() && report_.at("version")==acquisitionConditionsProtocolVersion &&
        report_.value("scope",std::string())=="before_first_samples" && report_.contains("origin") && oneOf(report_.at("origin"),{"not_reported","provider"}));
    for(const auto* key:{"frequency_calibration","amplitude_calibration"}) {
        require(report_.contains(key));const auto& value=report_.at(key);condition(value,{"unknown","uncalibrated","calibrated","not_applicable"});
        if(value.at("state")=="calibrated" || value.at("state")=="not_applicable")require(!value.at("evidence").empty());
        if(report_.at("origin")=="not_reported")require(value.at("state")=="unknown" && value.at("evidence").empty());
    }
    require(report_.contains("adc_overload"));const auto& adc=report_.at("adc_overload");condition(adc,{"unknown","detected","not_detected","not_applicable"});
    require(adc.contains("capability") && oneOf(adc.at("capability"),{"unknown","unavailable","available","not_applicable"}));
    if(adc.at("state")=="detected" || adc.at("state")=="not_detected")require(adc.at("capability")=="available" && !adc.at("evidence").empty());
    if(adc.at("capability")=="unknown" || adc.at("capability")=="unavailable")require(adc.at("state")=="unknown");
    require((adc.at("capability")=="not_applicable")== (adc.at("state")=="not_applicable"));
    if(adc.at("state")=="not_applicable")require(!adc.at("evidence").empty());
    if(report_.at("origin")=="not_reported")require(adc.at("capability")=="unknown" && adc.at("state")=="unknown" && adc.at("evidence").empty());
    require(report_.dump().size()<=64*1024);
}
void to_json(Json& value,const AcquisitionConditions& conditions){value=conditions.json();}
void from_json(const Json& value,AcquisitionConditions& conditions){conditions=AcquisitionConditions(value);}
}
