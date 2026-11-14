#include "../common.hpp"
namespace signalscope::models {
ReferenceModelProvider fsk() {
    const auto describe=[](const auto& context,const Json&){auto fields=common(context);fields.push_back(number("symbol_rate_hz",context.sample_rate/16.,.000001,context.sample_rate/2.,"Hz"));fields.push_back(number("deviation_hz",context.sample_rate/8.,0,context.sample_rate/2.,"Hz"));fields.push_back({{"key","bits"},{"type","string"},{"extension",true},{"label_key","bits"},{"default","01"}});return fields;};
    return provider("continuous-phase-2fsk","reference-fsk",describe,[describe](const auto& context,const Json& supplied){
        auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        const double amplitude=model.parameters.at("amplitude"),phase=model.parameters.at("phase_rad"),carrier=model.parameters.at("carrier_hz"),rate=model.parameters.at("symbol_rate_hz"),deviation=model.parameters.at("deviation_hz");const auto bits=model.parameters.at("bits").template get<std::string>();
        if(bits.empty() || bits.size()>256 || bits.find_first_not_of("01")!=std::string::npos)issue(model,"bits","issue.pattern");
        band(model,context,carrier-deviation,carrier+deviation,"deviation_hz");
        model.method={{"pulse","rectangular-frequency-continuous-phase"},{"pattern","repeated"},{"bit_zero_frequency_hz",carrier-deviation},{"bit_one_frequency_hz",carrier+deviation},{"spectrum","not-bandlimited-or-certified"}};
        if(model.issues.empty()) {
            if((bits.size()+1)*sizeof(int)>context.workspace_payload_bytes)throw std::length_error("FSK pattern exceeds its workspace budget");
            std::vector<int> prefix;prefix.reserve(bits.size()+1);prefix.push_back(0);for(const auto bit:bits)prefix.push_back(prefix.back()+(bit=='1'?1:-1));const auto bytes=prefix.capacity()*sizeof(int);
            generator(model,context,[=,prefix=std::move(prefix)](const ReferenceModelPoint& point){const auto length=prefix.size()-1;const auto t=point.seconds(),symbols=t*rate,periods=std::floor(symbols/length),within=symbols-periods*length;const auto index=std::min(length-1,static_cast<std::size_t>(std::floor(std::max(0.L,within))));const auto fraction=within-index;
                const auto integral=periods*prefix.back()+prefix[index]+(prefix[index+1]-prefix[index])*fraction;return oscillator(amplitude,phase,carrier*t+deviation/rate*integral);},bytes);
        }return model;
    });
}
}
