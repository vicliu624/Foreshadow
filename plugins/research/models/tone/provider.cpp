#include "../common.hpp"
namespace signalscope::models {
ReferenceModelProvider tone() {
    const auto describe=[](const auto& context,const Json&){return common(context);};
    return provider("constant-frequency","reference-tone",describe,[describe](const auto& context,const Json& supplied){
        auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        const double amplitude=model.parameters.at("amplitude"),phase=model.parameters.at("phase_rad"),frequency=model.parameters.at("carrier_hz");band(model,context,frequency,frequency,"carrier_hz");
        model.method={{"formula","A*exp(j*(phase+2*pi*f*t))"},{"time_support","continuous"}};
        if(model.issues.empty())generator(model,context,[=](const ReferenceModelPoint& point){return oscillator(amplitude,phase,frequency*point.seconds());});
        return model;
    });
}
}
