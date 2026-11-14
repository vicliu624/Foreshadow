#include "../common.hpp"
namespace signalscope::models {
ReferenceModelProvider chirp() {
    const auto describe=[](const auto& context,const Json&){auto fields=common(context);fields.push_back(number("start_hz",-double(context.sample_rate)/8.,-double(context.sample_rate)/2.,context.sample_rate/2.,"Hz"));fields.push_back(number("end_hz",context.sample_rate/8.,-double(context.sample_rate)/2.,context.sample_rate/2.,"Hz"));fields.push_back(number("duration_seconds",double(context.range.count)/context.sample_rate,1./context.sample_rate,600,"s"));return fields;};
    return provider("linear-chirp","reference-chirp",describe,[describe](const auto& context,const Json& supplied){
        auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        const double amplitude=model.parameters.at("amplitude"),phase=model.parameters.at("phase_rad"),carrier=model.parameters.at("carrier_hz"),start=model.parameters.at("start_hz"),end=model.parameters.at("end_hz"),duration=model.parameters.at("duration_seconds");const auto slope=(end-start)/duration;
        const auto a=carrier+start+slope*context.minimum_time_seconds,b=carrier+start+slope*context.maximum_time_seconds;band(model,context,std::min(a,b),std::max(a,b),"duration_seconds");
        model.method={{"formula","A*exp(j*(phase+2*pi*((fc+f0)*t+0.5*k*t^2)))"},{"slope_hz_per_second",slope},{"time_support","linear-extension-no-repeat"}};
        if(model.issues.empty())generator(model,context,[=](const ReferenceModelPoint& point){const auto t=point.seconds();return oscillator(amplitude,phase,(carrier+start)*t+.5L*slope*t*t);});
        return model;
    });
}
}
