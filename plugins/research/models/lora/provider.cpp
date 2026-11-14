#include "../common.hpp"
namespace signalscope::models {
ReferenceModelProvider lora() {
    const auto describe=[](const auto& context,const Json& pending){auto fields=common(context);fields.push_back(number("bandwidth_hz",std::min(125000.,context.sample_rate/2.),.000001,context.sample_rate,"Hz"));fields.push_back(integer("sf",7,5,12));
        unsigned sf=7;if(pending.contains("sf") && pending.at("sf").is_number_integer() && pending.at("sf")>=5 && pending.at("sf")<=12)sf=pending.at("sf").template get<unsigned>();
        fields.push_back(integer("symbol",0,0,(1u<<sf)-1));fields.push_back({{"key","direction"},{"type","enum"},{"extension",true},{"label_key","direction"},{"default","up"},{"choices",Json::array({"up","down"})},{"choice_labels",{{"up",{{"label_key","direction.up"}}},{"down",{{"label_key","direction.down"}}}}}});return fields;};
    return provider("lora-symbol-chirp","reference-lora",describe,[describe](const auto& context,const Json& supplied){
        auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        const double amplitude=model.parameters.at("amplitude"),phase=model.parameters.at("phase_rad"),carrier=model.parameters.at("carrier_hz"),bandwidth=model.parameters.at("bandwidth_hz");const auto m=1u<<model.parameters.at("sf").template get<unsigned>(),symbol=model.parameters.at("symbol").template get<unsigned>();const bool down=model.parameters.at("direction")=="down";
        band(model,context,carrier-bandwidth/2,carrier+bandwidth/2,"bandwidth_hz",true);const long double duration=static_cast<long double>(m)/bandwidth,wrap=duration*(1-static_cast<long double>(symbol)/m);
        model.method={{"reference","https://arxiv.org/abs/1906.04256"},{"equations","continuous-phase-integral-1-to-3-centered-at-zero"},{"symbol_duration_seconds",static_cast<double>(duration)},{"pattern","one-repeated-symbol"},{"packet_structure","not-modeled"},{"spectrum","instantaneous-sweep-is-not-occupied-bandwidth"}};
        if(model.issues.empty())generator(model,context,[=](const ReferenceModelPoint& point){const auto t=point.seconds();auto local=std::fmod(t,duration);if(local<0)local+=duration;
            const auto cycles=(-bandwidth/2.L+static_cast<long double>(symbol)*bandwidth/m)*local+bandwidth/(2*duration)*local*local-bandwidth*std::max(0.L,local-wrap);return oscillator(amplitude,phase,carrier*t+(down?-cycles:cycles));});
        return model;
    });
}
}
