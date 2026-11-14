#include "signalscope/backend/dsp/reference_model.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
PreparedReferenceModel prepared(const char* id,const Json& parameters,const ReferenceModelContext& context) {
    auto model=reference_model_provider(id).prepare(context,parameters);if(!model.issues.empty() || !model.evaluate)throw std::runtime_error(std::string("Builtin model rejected ")+id+": "+model.issues.dump());return model;
}
std::complex<double> exponential(double amplitude,long double phase){return amplitude*std::complex<double>{static_cast<double>(std::cos(phase)),static_cast<double>(std::sin(phase))};}
void scalarTruth() {
    const ReferenceModelContext context{8000,{(1ULL<<54)+11,16000},0,15999./8000};
    const Json common={{"amplitude",.7},{"phase_rad",.3},{"carrier_hz",200}};
    const auto tone=prepared("constant-frequency",common,context);auto parameters=common;parameters.update({{"modulation_hz",100},{"depth",1.5}});const auto am=prepared("sinusoidal-am",parameters,context);
    parameters=common;parameters.update({{"modulation_hz",100},{"deviation_hz",300}});const auto fm=prepared("sinusoidal-fm",parameters,context);
    for(unsigned n=0;n<16000;n+=7) {
        const long double t=static_cast<long double>(n)/8000,carrier=.3L+2*std::numbers::pi_v<long double>*200*t,modulation=2*std::numbers::pi_v<long double>*100*t;
        require(std::abs(tone.evaluate({n,8000,0})-exponential(.7,carrier))<2e-11,"Tone disagreed with an independent complex exponential");
        require(std::abs(am.evaluate({n,8000,0})-exponential(static_cast<double>(.7L*(1+1.5L*std::cos(modulation))),carrier))<2e-11,"AM envelope reversal or carrier phase was wrong");
        require(std::abs(fm.evaluate({n,8000,0})-exponential(.7,carrier+3*std::sin(modulation)))<2e-11,"FM deviation integration was wrong");
    }
    parameters=common;parameters.update({{"start_hz",-1000},{"end_hz",1000},{"duration_seconds",2}});const auto chirp=prepared("linear-chirp",parameters,context);
    long double phase=.3;
    for(unsigned n=0;n<16000;++n) {
        require(std::abs(chirp.evaluate({n,8000,0})-exponential(.7,phase))<2e-10,"Linear chirp disagreed with exact midpoint integration");
        const auto midpoint=(static_cast<long double>(n)+.5L)/8000;phase+=2*std::numbers::pi_v<long double>*(-800+1000*midpoint)/8000;
    }
    auto invalid=common;invalid.update({{"carrier_hz",3900},{"modulation_hz",200}});require(!reference_model_provider("sinusoidal-am").prepare(context,invalid).issues.empty(),"AM ignored its complete sideband configuration");
    auto shifted=context;shifted.minimum_time_seconds=-10;shifted.maximum_time_seconds=10;require(!reference_model_provider("linear-chirp").prepare(shifted,parameters).issues.empty(),"Chirp ignored alignment outside Nyquist");
}
void fskTruthAndResources() {
    const ReferenceModelContext context{8000,{0,16000},0,15999./8000};const Json parameters={{"amplitude",.7},{"phase_rad",.3},{"carrier_hz",200},{"symbol_rate_hz",500},{"deviation_hz",600},{"bits","00110"}};
    const auto fsk=prepared("continuous-phase-2fsk",parameters,context);long double phase=.3;auto source=std::make_shared<SignalWindow>();source->stream={8000,433000000,SampleFormat::cf32_le};source->source_id="independent-integrated-fsk";source->segments.push_back({(1ULL<<54)+101,{},{}});
    const std::string bits="00110";
    for(unsigned n=0;n<16000;++n) {
        const auto expected=exponential(.7,phase);require(std::abs(fsk.evaluate({n,8000,0})-expected)<3e-10,"CPFSK lost phase at repeated bit boundaries");source->segments[0].samples.emplace_back(static_cast<float>(expected.real()),static_cast<float>(expected.imag()));
        phase+=2*std::numbers::pi_v<long double>*(200+(bits[(n/16)%bits.size()]=='1'?600:-600))/8000;
    }
    require(std::abs(fsk.evaluate({0,8000,-1./8000})-exponential(.7,.3L+2*std::numbers::pi_v<long double>*400/8000))<1e-12,"Negative time alignment broke the repeated FSK pattern");
    const auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));ReferenceFitOptions options;options.fit_complex_gain=false;const auto compared=compare_reference_model(analysis,{analysis->first_sample(),16000},reference_model_provider("continuous-phase-2fsk"),parameters,options);
    require(compared.residual_rms<3e-8 && compared.method.at("provider_workspace_payload_bytes").get<std::size_t>()>0,"The actual comparison lost a builtin's phase or workspace accounting");
    for(const auto& provider:reference_model_providers()) {
        const auto defaults=provider.prepare(context,Json::object());require(defaults.issues.empty() && defaults.evaluate,"A builtin rejected its own typed defaults without an editor supplying values");
        auto descriptors=reference_model_parameters(provider,context);auto catalog=TextCatalog::resource(provider.resource_namespace);catalog.resolve(descriptors);
        for(const auto& descriptor:descriptors)require(descriptor.at("label").is_object() && descriptor.at("label").contains("en") && descriptor.at("label").contains("zh-CN"),"A builtin parameter lacks distributed resource labels");
        Json label={{"label_key",provider.label_key},{"description_key",provider.description_key}};catalog.resolve(label);require(label.at("label").is_object() && label.at("description").is_object(),"Builtin model name or explanation lacks resources");
    }
    auto invalid=parameters;invalid["bits"]="012";require(!reference_model_provider("continuous-phase-2fsk").prepare(context,invalid).issues.empty(),"FSK accepted an unknown bit mapping");
    auto limited=context;limited.workspace_payload_bytes=1;bool refused=false;try{reference_model_provider("continuous-phase-2fsk").prepare(limited,parameters);}catch(const std::length_error&){refused=true;}require(refused,"FSK allocated a pattern outside its declared workspace");
}
void loraIntegralTruth() {
    constexpr unsigned m=128;constexpr long double bandwidth=2000,duration=m/bandwidth;const auto pi=std::numbers::pi_v<long double>;
    for(const auto rate:{2000u,3000u,8000u})for(const auto symbol:{0u,37u,127u})for(const auto down:{false,true}) {
        const auto sampleCount=3*m*rate/2000;const ReferenceModelContext context{rate,{0,sampleCount},0,double(sampleCount-1)/rate};
        const Json parameters={{"amplitude",.7},{"phase_rad",.3},{"carrier_hz",0},{"bandwidth_hz",2000},{"sf",7},{"symbol",symbol},{"direction",down?"down":"up"}};
        const auto model=prepared("lora-symbol-chirp",parameters,context);const auto wrap=duration*(1-static_cast<long double>(symbol)/m);long double phase=.3;
        for(unsigned n=0;n<sampleCount;++n) {
            require(std::abs(model.evaluate({n,rate,0})-exponential(.7,phase))<3e-10,"LoRa disagreed with independent piecewise instantaneous-frequency integration");
            auto current=static_cast<long double>(n)/rate;const auto end=static_cast<long double>(n+1)/rate;
            while(current<end-1e-18L) {
                const auto interval=std::floor((current+1e-16L)/duration);const auto start=interval*duration,wrapped=start+wrap,boundary=(interval+1)*duration;
                const auto next=std::min(end,current<wrapped-1e-16L?wrapped:boundary);require(next>current,"Independent LoRa integration did not advance");
                const auto local=(current+next)/2-start;const auto frequency=-bandwidth/2+symbol*bandwidth/m+bandwidth/duration*local-(local>=wrap?bandwidth:0);
                phase+=2*pi*(down?-frequency:frequency)*(next-current);current=next;
            }
        }
    }
    const ReferenceModelContext context{2000,{0,m},0,(m-1)/2000.};const auto zero=prepared("lora-symbol-chirp",{{"bandwidth_hz",2000},{"sf",7},{"symbol",0}},context),shifted=prepared("lora-symbol-chirp",{{"bandwidth_hz",2000},{"sf",7},{"symbol",37}},context);
    for(unsigned k=0;k<m;++k)require(std::abs(shifted.evaluate({k,2000,0})*std::conj(zero.evaluate({k,2000,0}))-exponential(1,2*pi*37*k/m))<1e-10,"Critical-rate LoRa dechirp did not yield its known symbol tone");
    const auto provider=reference_model_provider("lora-symbol-chirp");const auto maximum=[&](unsigned sf){for(const auto& descriptor:reference_model_parameters(provider,context,{{"sf",sf}}))if(descriptor.at("key")=="symbol")return descriptor.at("maximum").get<unsigned>();return 0u;};
    require(maximum(5)==31 && maximum(12)==4095,"LoRa descriptors did not evaluate the whole pending SF configuration");require(!provider.prepare(context,{{"sf",5},{"symbol",37}}).issues.empty(),"LoRa accepted a symbol incompatible with the pending SF");
}
}
int main(){try{scalarTruth();fskTruthAndResources();loraIntegralTruth();std::cout<<"Builtin references: typed defaults, independent phase/envelope truth, LoRa wraps, dependent limits, resources and comparison passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
