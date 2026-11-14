#include "../common.hpp"
#include <charconv>
#include <cctype>

namespace signalscope::models {
namespace {
constexpr std::size_t maximumSymbols=256,maximumTextBytes=4096;
bool parse(const std::string& text,unsigned order,std::vector<std::complex<double>>* output,std::size_t& count) {
    count=0;std::string_view input=text;
    while(!input.empty()) {
        while(!input.empty() && (std::isspace(static_cast<unsigned char>(input.front())) || input.front()==','))input.remove_prefix(1);
        if(input.empty())break;
        unsigned symbol=0;const auto next=std::from_chars(input.data(),input.data()+input.size(),symbol);
        if(next.ec!=std::errc{} || next.ptr==input.data() || symbol>=order || ++count>maximumSymbols)return false;
        input.remove_prefix(static_cast<std::size_t>(next.ptr-input.data()));if(!input.empty() && !std::isspace(static_cast<unsigned char>(input.front())) && input.front()!=',')return false;
        if(output)output->push_back(oscillator(1,0,static_cast<long double>(symbol)/order));
    }
    return count>0;
}
long double pulse(long double x,long double beta) {
    const auto pi=std::numbers::pi_v<long double>;
    if(std::abs(x)<1e-12L)return 1-beta+4*beta/pi;
    if(beta==0)return std::sin(pi*x)/(pi*x);
    if(std::abs(std::abs(4*beta*x)-1)<1e-10L)return beta/std::sqrt(2.L)*((1+2/pi)*std::sin(pi/(4*beta))+(1-2/pi)*std::cos(pi/(4*beta)));
    return (std::sin(pi*(1-beta)*x)+4*beta*x*std::cos(pi*(1+beta)*x))/(pi*x*(1-16*beta*beta*x*x));
}
struct State {
    std::vector<std::complex<double>> symbols;
    double amplitude=1,phase=0,carrier=0,rate=1,beta=0;
    unsigned span=0;bool repeated=false,rrc=false;
    std::complex<double> symbol(std::int64_t index) const {
        if(repeated){index%=static_cast<std::int64_t>(symbols.size());if(index<0)index+=static_cast<std::int64_t>(symbols.size());}
        if(index<0 || static_cast<std::uint64_t>(index)>=symbols.size())throw std::out_of_range("PSK evaluation exceeds its known symbol support");
        return symbols[static_cast<std::size_t>(index)];
    }
    std::complex<double> evaluate(const ReferenceModelPoint& point) const {
        const auto time=point.seconds();auto position=point.offset_samples()*rate/point.sample_rate+static_cast<long double>(point.time_offset_seconds)*rate;if(repeated){const auto period=static_cast<long double>(symbols.size());position=std::fmod(position,period);if(position<0){position+=period;if(position>=period)position=std::nextafter(period,0.L);}}
        std::complex<double> value{};
        if(!rrc)value=symbol(static_cast<std::int64_t>(std::floor(position)));
        else {const auto center=position-.5L,half=span/2.L;const auto first=static_cast<std::int64_t>(std::ceil(center-half)),last=static_cast<std::int64_t>(std::floor(center+half));
            for(auto k=first;k<=last;++k)value+=symbol(k)*static_cast<double>(pulse(center-k,beta));}
        return value*oscillator(amplitude,phase,carrier*time);
    }
};
}
ReferenceModelProvider psk() {
    const auto describe=[](const ReferenceModelContext& context,const Json& pending){auto fields=common(context);
        fields.push_back({{"key","order"},{"type","enum"},{"extension",true},{"label_key","order"},{"default",4},{"choices",Json::array({2,4,8,16})}});
        fields.push_back(number("symbol_rate_hz",std::max(.000001,context.sample_rate/16.),.000001,context.sample_rate,"symbols/s"));
        fields.push_back({{"key","symbols"},{"type","string"},{"extension",true},{"label_key","symbols"},{"description_key","symbols.help"},{"default","0 1"}});
        fields.push_back({{"key","sequence_mode"},{"type","enum"},{"extension",true},{"label_key","sequence_mode"},{"default","repeat"},{"choices",Json::array({"repeat","finite"})},{"choice_labels",{{"repeat",{{"label_key","sequence.repeat"}}},{"finite",{{"label_key","sequence.finite"}}}}}});
        fields.push_back({{"key","pulse_shape"},{"type","enum"},{"extension",true},{"label_key","pulse_shape"},{"default","rectangular"},{"choices",Json::array({"rectangular","rrc"})},{"choice_labels",{{"rectangular",{{"label_key","pulse.rectangular"}}},{"rrc",{{"label_key","pulse.rrc"}}}}}});
        auto beta=number("rolloff",.35,0,1);beta["enabled_when"]={{"key","pulse_shape"},{"equals","rrc"}};beta["inactive_allowed"]=true;fields.push_back(beta);
        auto span=integer("pulse_span_symbols",8,2,32);span["enabled_when"]={{"key","pulse_shape"},{"equals","rrc"}};span["inactive_allowed"]=true;fields.push_back(span);
        (void)pending;return fields;};
    return provider("known-symbol-psk","reference-psk",describe,[describe](const ReferenceModelContext& context,const Json& supplied){auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        State state;state.amplitude=model.parameters.at("amplitude");state.phase=model.parameters.at("phase_rad");state.carrier=model.parameters.at("carrier_hz");state.rate=model.parameters.at("symbol_rate_hz");state.beta=model.parameters.at("rolloff");state.span=model.parameters.at("pulse_span_symbols");state.repeated=model.parameters.at("sequence_mode")=="repeat";state.rrc=model.parameters.at("pulse_shape")=="rrc";
        const auto& text=model.parameters.at("symbols").get_ref<const std::string&>();const unsigned order=model.parameters.at("order");std::size_t count=0;
        if(text.size()>maximumTextBytes || !parse(text,order,nullptr,count)){issue(model,"symbols","issue.symbols");return model;}
        if(!state.repeated) {const auto low=context.minimum_time_seconds*state.rate,high=context.maximum_time_seconds*state.rate;
            if(state.rrc?(low-.5-state.span/2.<0 || high-.5+state.span/2.>=count):(low<0 || high>=count))issue(model,"symbols","issue.support");}
        if(state.rrc){const auto half=state.rate*(1+state.beta)/2;band(model,context,state.carrier-half,state.carrier+half,"symbol_rate_hz",true);}else band(model,context,state.carrier,state.carrier,"carrier_hz");
        if(!model.issues.empty())return model;
        if(sizeof(State)>context.workspace_payload_bytes || count*sizeof(std::complex<double>)>context.workspace_payload_bytes-sizeof(State))throw std::length_error("PSK reference exceeds its generator workspace");
        state.symbols.reserve(count);std::size_t ignored=0;(void)parse(text,order,&state.symbols,ignored);const auto extra=state.symbols.capacity()*sizeof(std::complex<double>);
        model.method={{"mapping","symbol-index-k-to-exp(j*2*pi*k/M); no bit or Gray mapping inferred"},{"symbol_count",count},{"symbol_period_seconds",1/state.rate},{"symbol_center_offset",.5},
            {"pulse",state.rrc?"symmetric-truncated-root-raised-cosine":"rectangular-symbol-hold"},{"pulse_normalization",state.rrc?"unit-energy-in-symbol-time-before-truncation; no truncation renormalization":"unit-magnitude-symbols"},
            {"sequence",state.repeated?"explicitly-repeated":"finite-known-support-only"},{"reference_conditions",{{"symbols","supplied"},{"pulse_and_clock","manual"},{"carrier_synchronization","not-established"},{"symbol_synchronization","not-established"},{"equalization","not-performed"}}},
            {"measurement_domain","time-samples"},{"evm","not-established"},{"spectrum","rectangular-or-truncated-pulses-do-not-prove-occupied-bandwidth"},{"maximum_symbol_terms_per_evaluation",state.rrc?state.span+1:1}};
        generator(model,context,[state=std::move(state)](const ReferenceModelPoint& point){return state.evaluate(point);},extra);return model;
    },3);
}
}
