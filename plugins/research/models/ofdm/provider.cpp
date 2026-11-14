#include "../common.hpp"
#include <charconv>
#include <cctype>

namespace signalscope::models {
namespace {
constexpr std::size_t maximumCarriers=256,maximumCarriersPerSymbol=64,maximumSymbols=16,maximumTextBytes=4096;
struct Carrier {int bin;std::complex<double> value;};
struct State {
    std::vector<Carrier> carriers;std::vector<unsigned> starts;
    double amplitude=1,phase=0,carrier=0;unsigned fft=64,prefix=16,rate=1;bool repeated=false;
    std::complex<double> evaluate(const ReferenceModelPoint& point) const {
        const auto time=point.seconds(),length=static_cast<long double>(fft)+prefix;auto position=point.offset_samples()*rate/point.sample_rate+static_cast<long double>(point.time_offset_seconds)*rate;const auto symbols=starts.size()-1;
        if(repeated){const auto period=length*symbols;position=std::fmod(position,period);if(position<0){position+=period;if(position>=period)position=std::nextafter(period,0.L);}}
        if(position<0 || position>=length*symbols)throw std::out_of_range("OFDM evaluation exceeds its known symbol support");
        const auto symbol=static_cast<std::size_t>(std::floor(position/length));const auto local=position-symbol*length-prefix;std::complex<double> value{};
        for(auto i=starts[symbol];i<starts[symbol+1];++i)value+=carriers[i].value*oscillator(1,0,static_cast<long double>(carriers[i].bin)*local/fft);
        return value*oscillator(amplitude/std::sqrt(double(fft)),phase,carrier*time);
    }
};
struct Parser {
    std::string_view input;unsigned fft=64;std::size_t carriers=0,symbols=1,current=0;State* output=nullptr;
    void whitespace(){while(!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))input.remove_prefix(1);}
    bool token(char expected){whitespace();if(input.empty() || input.front()!=expected)return false;input.remove_prefix(1);return true;}
    template<class T>bool number(T& value){whitespace();if(!input.empty() && input.front()=='+')input.remove_prefix(1);const auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);if(parsed.ec!=std::errc{} || parsed.ptr==input.data())return false;input.remove_prefix(static_cast<std::size_t>(parsed.ptr-input.data()));return true;}
    bool parse() {
        if(output)output->starts.push_back(0);
        for(;;) {
            whitespace();if(input.empty())break;
            if(input.front()=='|'){if(!current || ++symbols>maximumSymbols)return false;current=0;input.remove_prefix(1);if(output)output->starts.push_back(static_cast<unsigned>(output->carriers.size()));continue;}
            int bin=0;double real=0,imaginary=0;if(!number(bin) || !token(':') || !number(real) || !token(',') || !number(imaginary))return false;
            if(bin< -static_cast<int>(fft/2) || bin>static_cast<int>((fft-1)/2) || !std::isfinite(real) || !std::isfinite(imaginary) || std::abs(real)>10 || std::abs(imaginary)>10 || ++carriers>maximumCarriers || ++current>maximumCarriersPerSymbol)return false;
            if(!input.empty() && input.front()!='|' && !std::isspace(static_cast<unsigned char>(input.front())))return false;
            if(output){for(auto i=output->starts.back();i<output->carriers.size();++i)if(output->carriers[i].bin==bin)return false;output->carriers.push_back({bin,{real,imaginary}});}
        }
        if(output)output->starts.push_back(static_cast<unsigned>(output->carriers.size()));
        return current>0;
    }
};
}
ReferenceModelProvider ofdm() {
    const auto describe=[](const ReferenceModelContext& context,const Json& pending){auto fields=common(context);fields.push_back(integer("fft_size",64,8,4096));unsigned fft=64;if(pending.contains("fft_size") && pending.at("fft_size").is_number_integer() && pending.at("fft_size")>=8 && pending.at("fft_size")<=4096)fft=pending.at("fft_size").get<unsigned>();
        fields.push_back(integer("prefix_samples",std::min(16u,fft),0,fft));fields.push_back({{"key","subcarriers"},{"type","string"},{"extension",true},{"label_key","subcarriers"},{"description_key","subcarriers.help"},{"default","-3:1,0 -1:0,1 1:-1,0 3:0,-1"}});
        fields.push_back({{"key","sequence_mode"},{"type","enum"},{"extension",true},{"label_key","sequence_mode"},{"default","repeat"},{"choices",Json::array({"repeat","finite"})},{"choice_labels",{{"repeat",{{"label_key","sequence.repeat"}}},{"finite",{{"label_key","sequence.finite"}}}}}});return fields;};
    return provider("known-subcarrier-ofdm","reference-ofdm",describe,[describe](const ReferenceModelContext& context,const Json& supplied){auto model=values(describe(context,supplied),supplied);if(!model.issues.empty())return model;
        State state;state.amplitude=model.parameters.at("amplitude");state.phase=model.parameters.at("phase_rad");state.carrier=model.parameters.at("carrier_hz");state.fft=model.parameters.at("fft_size");state.prefix=model.parameters.at("prefix_samples");state.rate=context.sample_rate;state.repeated=model.parameters.at("sequence_mode")=="repeat";
        const auto& text=model.parameters.at("subcarriers").get_ref<const std::string&>();Parser count{text,state.fft};if(text.size()>maximumTextBytes || !count.parse()){issue(model,"subcarriers","issue.subcarriers");return model;}
        if(!state.repeated && (context.minimum_time_seconds<0 || context.maximum_time_seconds*state.rate>=count.symbols*(std::uint64_t(state.fft)+state.prefix)))issue(model,"subcarriers","issue.support");
        const auto bytes=count.carriers*sizeof(Carrier)+(count.symbols+1)*sizeof(unsigned);if(sizeof(State)>context.workspace_payload_bytes || bytes>context.workspace_payload_bytes-sizeof(State))throw std::length_error("OFDM reference exceeds its generator workspace");
        state.carriers.reserve(count.carriers);state.starts.reserve(count.symbols+1);Parser fill{text,state.fft,0,1,0,&state};if(!fill.parse()){issue(model,"subcarriers","issue.subcarriers");return model;}
        double energy=0;for(const auto& carrier:state.carriers){const auto frequency=state.carrier+double(carrier.bin)*state.rate/state.fft;band(model,context,frequency,frequency,"carrier_hz",true);energy+=std::norm(carrier.value);}if(!(energy>0))issue(model,"subcarriers","issue.energy");if(!model.issues.empty())return model;
        model.method={{"formula","sum(X[k]*exp(j*2*pi*k*(sample-in-symbol-CP)/N))/sqrt(N)"},{"fft_size",state.fft},{"prefix_samples",state.prefix},{"reference_sample_rate_hz",state.rate},
            {"subcarrier_spacing_hz",double(state.rate)/state.fft},{"known_symbols",count.symbols},{"specified_carriers",count.carriers},{"maximum_carrier_terms_per_evaluation",maximumCarriersPerSymbol},
            {"unspecified_bins","explicit-zero-reference"},{"sequence",state.repeated?"explicitly-repeated":"finite-known-support-only"},{"prefix","last-CP-samples-of-each-useful-symbol"},
            {"reference_conditions",{{"complex_subcarriers","supplied-for-each-symbol"},{"FFT_clock_and_boundary","manual"},{"carrier_synchronization","not-established"},{"symbol_synchronization","not-established"},{"equalization","not-performed"}}},
            {"measurement_domain","time-samples-including-explicit-prefix"},{"evm","not-established"},{"spectrum","rectangular-symbol-boundaries-do-not-prove-occupied-bandwidth"}};
        const auto extra=state.carriers.capacity()*sizeof(Carrier)+state.starts.capacity()*sizeof(unsigned);generator(model,context,[state=std::move(state)](const ReferenceModelPoint& point){return state.evaluate(point);},extra);return model;
    },3);
}
}
