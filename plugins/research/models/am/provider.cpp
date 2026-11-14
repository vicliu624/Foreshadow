#include "../common.hpp"
namespace signalscope::models {
ReferenceModelProvider am() {
    const auto describe=[](const auto& context,const Json&){auto fields=common(context);fields.push_back(number("modulation_hz",context.sample_rate/64.,.000001,context.sample_rate/2.,"Hz"));fields.push_back(number("depth",.5,0,2));return fields;};
    return provider("sinusoidal-am","reference-am",describe,[describe](const auto& context,const Json& supplied){
        auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        const double amplitude=model.parameters.at("amplitude"),phase=model.parameters.at("phase_rad"),carrier=model.parameters.at("carrier_hz"),modulation=model.parameters.at("modulation_hz"),depth=model.parameters.at("depth");
        band(model,context,carrier-modulation,carrier+modulation,"modulation_hz");model.method={{"formula","A*(1+m*cos(2*pi*fm*t))*exp(j*(phase+2*pi*fc*t))"},{"envelope_can_reverse",depth>1}};
        if(model.issues.empty())generator(model,context,[=](const ReferenceModelPoint& point){const auto t=point.seconds();const auto envelope=1+depth*std::cos(2*std::numbers::pi_v<long double>*std::remainder(modulation*t,1.L));return oscillator(static_cast<double>(amplitude*envelope),phase,carrier*t);});
        return model;
    });
}
}
