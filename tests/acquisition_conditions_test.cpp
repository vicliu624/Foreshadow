#include "signalscope/backend/iq/acquisition_conditions.hpp"
#include <iostream>
#include <stdexcept>
using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
}
int main(){try{
    const auto legacy=AcquisitionConditions{}.json();
    require(legacy.at("origin")=="not_reported" && legacy.at("adc_overload").at("state")=="unknown" && legacy.at("frequency_calibration").at("state")=="unknown","Legacy readback invented calibration or a clear ADC");
    require(Json::parse(legacy.dump()).get<AcquisitionConditions>().json()==legacy,"Unknown readback could not round-trip");
    auto reference=legacy;reference["origin"]="provider";reference["frequency_calibration"]={{"state","calibrated"},{"evidence",{{"reference_id","independent.clock.fixture"},{"correction_ppm",2.5}}}};
    reference["adc_overload"]={{"capability","available"},{"state","detected"},{"evidence",{{"sensor","independent.adc"},{"counter",3}}}};
    reference["independent_new_condition"]={{"unknown_data",Json::array({1,2,3})}};
    const auto reported=Json::parse(reference.dump()).get<AcquisitionConditions>();
    require(reported.json()==reference && reported.json().at("scope")=="before_first_samples","Provider evidence, scope or unknown fields were discarded");
    auto unavailable=reference;unavailable["adc_overload"]={{"capability","unavailable"},{"state","unknown"},{"evidence",{{"reason_code","not_exposed_by_provider"}}}};
    require(AcquisitionConditions(unavailable).json().at("adc_overload").at("state")=="unknown","Unavailable overload sensor became a clear ADC");
    auto invalid=reference;invalid["frequency_calibration"]["evidence"]=Json::object();rejected([&]{AcquisitionConditions check(invalid);},"Calibration without evidence accepted");
    invalid=unavailable;invalid["adc_overload"]["state"]="not_detected";rejected([&]{AcquisitionConditions check(invalid);},"Unavailable monitor claimed no overload");
    invalid=reference;invalid["scope"]="entire_recording";rejected([&]{AcquisitionConditions check(invalid);},"Startup report pretended to cover a recording");
    invalid=reference;invalid["origin"]="not_reported";rejected([&]{AcquisitionConditions check(invalid);},"Legacy fallback claimed known provider facts");
    invalid=reference;invalid["version"]=2;rejected([&]{AcquisitionConditions check(invalid);},"Unknown conditions protocol accepted");
    invalid=reference;invalid["extra"]=std::string(65536,'x');rejected([&]{AcquisitionConditions check(invalid);},"Unbounded startup evidence accepted");
    invalid=reference;invalid["adc_overload"]["state"]="not_applicable";rejected([&]{AcquisitionConditions check(invalid);},"Contradictory overload capability accepted");
    std::cout<<"Startup acquisition evidence, unknown fallback, scope, extensions and invalid claims passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
