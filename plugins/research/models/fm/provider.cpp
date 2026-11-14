#include "../common.hpp"
namespace signalscope::models {
ReferenceModelProvider fm() {
    const auto describe=[](const auto& context,const Json&){auto fields=common(context);fields.push_back(number("modulation_hz",context.sample_rate/64.,.000001,context.sample_rate/2.,"Hz"));fields.push_back(number("deviation_hz",context.sample_rate/16.,0,context.sample_rate/2.,"Hz"));return fields;};
    return provider("sinusoidal-fm","reference-fm",describe,[describe](const auto& context,const Json& supplied){
        auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        const double amplitude=model.parameters.at("amplitude"),phase=model.parameters.at("phase_rad"),carrier=model.parameters.at("carrier_hz"),modulation=model.parameters.at("modulation_hz"),deviation=model.parameters.at("deviation_hz");
        band(model,context,carrier-deviation,carrier+deviation,"deviation_hz");model.method={{"formula","A*exp(j*(phase+2*pi*fc*t+(df/fm)*sin(2*pi*fm*t)))"},{"spectrum","not-bandlimited-or-certified"}};
        if(model.issues.empty())generator(model,context,[=](const ReferenceModelPoint& point){const auto t=point.seconds();const auto cycles=carrier*t+deviation/modulation/(2*std::numbers::pi_v<long double>)*std::sin(2*std::numbers::pi_v<long double>*std::remainder(modulation*t,1.L));return oscillator(amplitude,phase,cycles);});
        return model;
    });
}
}
