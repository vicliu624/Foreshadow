#include "signalscope/backend/dsp/reference_model.hpp"
#include <kiss_fft.h>
#include <array>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <numbers>
#include <limits>
#include <sstream>
using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
PreparedReferenceModel prepared(const char* id,const Json& parameters,const ReferenceModelContext& context){auto value=reference_model_provider(id).prepare(context,parameters);if(!value.issues.empty() || !value.evaluate)throw std::runtime_error(std::string(id)+": "+value.issues.dump());return value;}
std::complex<double> phase(long double angle){return {static_cast<double>(std::cos(angle)),static_cast<double>(std::sin(angle))};}
void pskTruth() {
    const ReferenceModelContext context{16000,{(1ULL<<54)+29,1600},-.01,.12};const auto pi=std::numbers::pi_v<long double>;
    for(const auto order:{2u,4u,8u,16u}) {
        const std::array<unsigned,4> symbols{0,order-1,order/2,1};std::ostringstream pattern;for(const auto symbol:symbols)pattern<<symbol<<' ';
        const Json parameters={{"order",order},{"symbols",pattern.str()},{"symbol_rate_hz",1000},{"amplitude",.7},{"phase_rad",.3},{"carrier_hz",213}};const auto model=prepared("known-symbol-psk",parameters,context);
        for(unsigned n=0;n<1600;++n){const auto expected=.7*phase(.3L+2*pi*(213*static_cast<long double>(n)/16000+static_cast<long double>(symbols[(n/16)%4])/order));require(std::abs(model.evaluate({n,16000,0})-expected)<1e-10,"Known PSK natural mapping, boundaries or carrier phase was wrong");}
        require(std::abs(model.evaluate({0,16000,-1./16000})-.7*phase(.3L+2*pi*(-213.L/16000+1.L/order)))<1e-11,"Repeated PSK negative alignment lost its last known symbol");
        require(std::abs(model.evaluate({0,16000,-std::numeric_limits<double>::denorm_min()})-.7*phase(.3L+2*pi/order))<1e-11,"Subnormal negative PSK time crossed to the first symbol");
        require(model.method.at("reference_conditions").at("symbol_synchronization")=="not-established" && model.method.at("evm")=="not-established","A known PSK pattern was promoted to synchronized symbol EVM");
    }
    const auto provider=reference_model_provider("known-symbol-psk");for(const auto* text:{"","-1","2","0x1","0 1 trailing","9999999999999999999999","nan"})require(!provider.prepare(context,{{"order",2},{"symbols",text}}).issues.empty(),"PSK accepted an invalid index pattern");
    std::string many;for(unsigned i=0;i<257;++i)many+="0 ";require(!provider.prepare(context,{{"symbols",many}}).issues.empty(),"PSK ignored its known-symbol budget");
    require(!provider.prepare(context,{{"symbols","0 1"},{"sequence_mode","finite"}}).issues.empty(),"Finite PSK silently repeated an unsupported reference");
    auto tiny=context;tiny.workspace_payload_bytes=1;bool refused=false;try{provider.prepare(tiny,Json::object());}catch(const std::length_error&){refused=true;}require(refused,"PSK ignored actual generator capacity");
}
// Independent inverse transform of sqrt(raised-cosine spectrum), with numerical
// quadrature. This does not reproduce the provider's time-domain RRC formula.
long double spectralPulse(long double x,long double beta) {
    const auto pi=std::numbers::pi_v<long double>;if(beta==0)return std::abs(x)<1e-16L?1:std::sin(pi*x)/(pi*x);
    const auto flat=(1-beta)/2,edge=(1+beta)/2;const auto integral=[&](long double begin,long double end,bool transition){constexpr unsigned n=4096;const auto step=(end-begin)/n;long double value=0;
        for(unsigned i=0;i<=n;++i){const auto f=begin+i*step;const auto response=transition?std::sqrt(std::max(0.L,(1+std::cos(pi*(f-flat)/beta))/2)):1.L;value+=(i==0 || i==n?1:i%2?4:2)*response*std::cos(2*pi*f*x);}return value*step/3;};return 2*(integral(0,flat,false)+integral(flat,edge,true));
}
void rrcTruth() {
    std::string zero,one;for(unsigned i=0;i<32;++i){zero+="0 ";one+=i==12?"1 ":"0 ";}
    for(const auto beta:{0.,.25,.35,.5,1.}) {
        const ReferenceModelContext context{8000,{0,640},.08,.17};Json parameters={{"order",2},{"symbols",zero},{"symbol_rate_hz",100},{"pulse_shape","rrc"},{"rolloff",beta},{"pulse_span_symbols",8},{"sequence_mode","finite"}};
        const auto baseline=prepared("known-symbol-psk",parameters,context);parameters["symbols"]=one;const auto changed=prepared("known-symbol-psk",parameters,context);
        std::vector<long double> points{0,.1,.7,1.1,2.75,3.99};if(beta>0){points.push_back(1/(4*static_cast<long double>(beta)));points.push_back(-1/(4*static_cast<long double>(beta)));}
        for(const auto x:points){const double time=static_cast<double>((12.5L+x)/100);const auto actual=(baseline.evaluate({0,8000,time})-changed.evaluate({0,8000,time}))/2.;require(std::abs(actual-std::complex<double>(static_cast<double>(spectralPulse(x,beta)),0))<2e-9,"RRC disagreed with independent frequency-domain quadrature or a removable singularity");}
        const double outside=(12.5+4.1)/100;require(std::abs(baseline.evaluate({0,8000,outside})-changed.evaluate({0,8000,outside}))<1e-12,"RRC extended a pulse beyond its declared truncation");
    }
    auto boundary=ReferenceModelContext{8000,{0,128},0,.015};require(!reference_model_provider("known-symbol-psk").prepare(boundary,{{"symbols",zero},{"symbol_rate_hz",100},{"pulse_shape","rrc"},{"sequence_mode","finite"}}).issues.empty(),"Finite RRC invented unavailable neighboring pulse support");
    const ReferenceModelContext band{8000,{0,128},0,.015};require(!reference_model_provider("known-symbol-psk").prepare(band,{{"symbol_rate_hz",7000},{"pulse_shape","rrc"},{"rolloff",1}}).issues.empty(),"RRC ignored its complete nominal design band");
}
std::vector<std::complex<double>> inverse(unsigned n,const std::vector<std::pair<int,std::complex<double>>>& carriers) {
    std::vector<kiss_fft_cpx> input(n),output(n);for(const auto& [bin,value]:carriers){const auto k=bin<0?static_cast<int>(n)+bin:bin;input[static_cast<std::size_t>(k)]={static_cast<kiss_fft_scalar>(value.real()),static_cast<kiss_fft_scalar>(value.imag())};}
    auto* plan=kiss_fft_alloc(static_cast<int>(n),1,nullptr,nullptr);require(plan,"No independent FFT plan");kiss_fft(plan,input.data(),output.data());std::free(plan);std::vector<std::complex<double>> values;for(const auto& value:output)values.emplace_back(value.r/std::sqrt(double(n)),value.i/std::sqrt(double(n)));return values;
}
void ofdmTruth() {
    const auto pi=std::numbers::pi_v<long double>;
    const std::array<std::vector<std::pair<int,std::complex<double>>>,2> carriers{{{{-2,{.3,-.5}},{1,{-1.2,.4}}},{{-1,{.2,.8}},{2,{-.4,-.9}}}}};
    for(const auto n:{8u,63u,64u})for(const auto cp:{0u,n/4,n}) {
        const unsigned rate=8000,length=n+cp,count=length*2;const ReferenceModelContext context{rate,{(1ULL<<54)+23,count},0,double(count-1)/rate};const Json parameters={{"fft_size",n},{"prefix_samples",cp},{"subcarriers","-2:0.3,-0.5 1:-1.2,0.4 | -1:0.2,0.8 2:-0.4,-0.9"},{"sequence_mode","finite"},{"amplitude",.7},{"phase_rad",.4},{"carrier_hz",43}};
        const auto model=prepared("known-subcarrier-ofdm",parameters,context);auto source=std::make_shared<SignalWindow>();source->stream={rate,433000000,SampleFormat::cf32_le};source->segments.push_back({context.range.first,{},{}});
        for(unsigned symbol=0;symbol<2;++symbol){const auto expected=inverse(n,carriers[symbol]);double energy=0;for(unsigned i=0;i<n;++i)energy+=std::norm(expected[i]);double inputEnergy=0;for(const auto& item:carriers[symbol])inputEnergy+=std::norm(item.second);require(std::abs(energy-inputEnergy)<2e-6,"The independent unitary FFT violated Parseval");
            for(unsigned i=0;i<length;++i){const auto sample=symbol*length+i;const auto actual=model.evaluate({sample,rate,0});const auto truth=.7*expected[(i+n-cp)%n]*phase(.4L+2*pi*43*sample/rate);require(std::abs(actual-truth)<5e-7,"OFDM disagreed with independent inverse FFT, prefix or symbol sequencing");source->segments.back().samples.emplace_back(static_cast<float>(truth.real()),static_cast<float>(truth.imag()));}}
        auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));ReferenceFitOptions options;options.fit_complex_gain=false;const auto comparison=compare_reference_model(analysis,context.range,reference_model_provider("known-subcarrier-ofdm"),parameters,options);require(comparison.residual_rms<5e-7 && comparison.method.at("evm")=="not-established" && comparison.method.at("provider_method").at("reference_conditions").at("equalization")=="not-performed","A known OFDM waveform was mislabeled as equalized subcarrier EVM");
        auto uncovered=context;uncovered.maximum_time_seconds=double(count)/rate;require(!reference_model_provider("known-subcarrier-ofdm").prepare(uncovered,parameters).issues.empty(),"Finite OFDM silently extended its last symbol");
    }
    const ReferenceModelContext context{8000,{0,256},-.1,.1};const auto single=prepared("known-subcarrier-ofdm",{{"fft_size",64},{"prefix_samples",16},{"subcarriers","3:1,0"}},context);
    for(const auto shift:{-.003,.00003125,.01003125}){auto position=shift*8000;position=std::fmod(position,80.);if(position<0)position+=80;const auto truth=phase(2*pi*3*(position-16)/64)/8.;require(std::abs(single.evaluate({0,8000,shift})-truth)<1e-11,"Fractional or negative OFDM alignment changed its explicit prefix phase");}
    require(std::abs(single.evaluate({0,8000,-std::numeric_limits<double>::denorm_min()})-std::complex<double>(.125,0))<1e-11,"Subnormal negative OFDM time escaped the repeated reference domain");
    const auto provider=reference_model_provider("known-subcarrier-ofdm");for(const auto* bad:{"","| -1:1,0","-1:1,0 |","-1:1,0 -1:0,1","32:1,0","-33:1,0","1:nan,0","1:1,inf","1:11,0","1:1,0junk","1:0,0","1:1 0"})require(!provider.prepare(context,{{"subcarriers",bad}}).issues.empty(),"OFDM accepted a malformed, duplicate, out-of-range or zero-energy reference");
    const auto descriptors=reference_model_parameters(provider,context,{{"fft_size",8}});bool bounded=false;for(const auto& descriptor:descriptors)if(descriptor.at("key")=="prefix_samples")bounded=descriptor.at("maximum")==8;require(bounded,"OFDM prefix limits ignored the entire pending FFT configuration");
    auto tiny=context;tiny.workspace_payload_bytes=1;bool refused=false;try{provider.prepare(tiny,Json::object());}catch(const std::length_error&){refused=true;}require(refused,"OFDM ignored generator vector capacity");
}
}
int main(){try{pskTruth();rrcTruth();ofdmTruth();std::cout<<"Known PSK/RRC and OFDM references: independent spectral quadrature and FFT, finite support, prefix/clock, budgets and EVM refusal passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
