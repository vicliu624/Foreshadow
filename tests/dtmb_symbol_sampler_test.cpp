#include "src/adapters/television/dtmb/symbol_sampler.hpp"
#include "src/adapters/television/dtmb/clock_estimator.hpp"
#include "dtmb/iq.hpp"
#include <complex>
#include <array>
#include <cstring>
#include <iostream>
#include <random>
using namespace signalscope::television::native;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::vector<float> sample(std::span<const float> input,unsigned rate,unsigned phase,Workers* workers=nullptr) {
    SymbolSampler sampler(rate,7560000,phase,workers);std::vector<float> output;
    for(std::size_t i=0;i<input.size();i+=8192)sampler.process(input.subspan(i,std::min<std::size_t>(8192,input.size()-i)),output);
    sampler.finish(output);return output;
}
}
int main() {
    try {
        // Figure 12's D1..D10=0000000001, read D10 before clocking.
        // This literal is independent of the native dependency's own PN test.
        const auto pn595=dtmb::core::pn_reference_chips(dtmb::core::PnMode::pn595);
        constexpr std::uint32_t prefix=0x80249a6b;
        for(unsigned i=0;i<32;++i)require(pn595[i]==((prefix>>(31-i))&1?-1:1),"Native PN595 does not match the normative register initialization");
        std::mt19937 random(564);std::uniform_real_distribution<float> value(-1,1);std::vector<float> input(50000*2);
        for(auto& item:input)item=value(random);Workers workers(4);
        for(const auto rate:{7560000u,10000000u,10000001u,16000000u}) {
            dtmb::core::IqResampler original(rate,7560000);std::vector<float> expected;original.process(input,expected);original.finish(expected);
            const auto actual=sample(input,rate,0),parallel=sample(input,rate,0,&workers);
            require(actual==expected,"Zero-phase sampling changed the pinned upstream resampler's exact output");
            require(parallel==actual,"Worker count or chunk boundaries changed sample values");
        }
        // Independent analytic complex tone: fractional output phase must
        // rotate the actual signal by its continuous-time phase, not only
        // alter a metadata field or select an adjacent integer sample.
        constexpr unsigned rate=16000000,outputRate=7560000;constexpr double frequency=outputRate/7.;
        for(std::size_t i=0;i<input.size()/2;++i){const auto tone=std::polar(1.,2*std::numbers::pi*frequency*i/rate);input[2*i]=tone.real();input[2*i+1]=tone.imag();}
        const auto zero=sample(input,rate,0);
        for(const auto phase:{1u,5u,8u,15u}) {
            const auto shifted=sample(input,rate,phase);std::complex<double> cross=0;double power=0;
            for(std::size_t i=128;i+128<std::min(zero.size(),shifted.size())/2;++i){const std::complex<double> a(zero[2*i],zero[2*i+1]),b(shifted[2*i],shifted[2*i+1]);cross+=b*std::conj(a);power+=std::norm(a);}
            const auto gain=cross/power,expected=std::polar(1.,2*std::numbers::pi*frequency/outputRate*phase/SymbolSampler::phaseCount);
            require(std::abs(std::arg(gain/expected))<.002 && std::abs(std::abs(gain)-1)<.01,"Fractional sampler does not match the independent continuous-time tone");
        }
        const auto full=sample(input,rate,5);constexpr std::size_t firstInput=20000,firstOutput=10000,window=8000;
        SymbolSampler local(rate,outputRate,5,nullptr,firstInput,firstOutput);std::vector<float> slice;
        local.process(std::span(input).subspan(firstInput*2,window*2),slice);local.finish(slice);
        require(slice.size()>1000 && std::equal(slice.begin(),slice.begin()+1000,full.begin()+firstOutput*2),"A timing probe changed the full-stream phase when it used a later input window");
        bool rejected=false;try{SymbolSampler invalid(rate,outputRate,16);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid timing phase was accepted");
        rejected=false;try{local.process(input,slice);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"The sampler accepted data after EOF");
        rejected=false;try{workers.run(64,[](auto first,auto last){if(first<=17 && last>17)throw std::runtime_error("fixture failure");},1);}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"A native worker exception was lost");std::array<unsigned,512> counts{};
        workers.run(counts.size(),[&](auto first,auto last){for(auto i=first;i<last;++i)++counts[i];},1);
        require(std::all_of(counts.begin(),counts.end(),[](auto n){return n==1;}),"The bounded worker group did not recover after a failed job");
        // Independent periodic multitone source with a non-integer repeat
        // interval. This anchors fractional clock estimation without using
        // the receiver's resampler to generate its own expected answer.
        constexpr unsigned nominal=1000000;constexpr double period=125000.625;
        std::vector<float> repeated(300000*2,0);
        for(unsigned tone=0;tone<32;++tone) {
            const auto harmonic=1000+(tone*7919u)%40000;
            auto carrier=std::polar(1./std::sqrt(32.),double(tone));const auto step=std::polar(1.,2*std::numbers::pi*harmonic/period);
            for(std::size_t i=0;i<repeated.size()/2;++i){repeated[2*i]+=carrier.real();repeated[2*i+1]+=carrier.imag();carrier*=step;}
        }
        const auto clock=estimateClock(repeated,nominal,outputRate,workers);
        require(clock.locked && std::abs(clock.ppm-5)<.03,"Fractional clock recovery does not match the independent periodic waveform");
        auto carrier=std::complex<double>(1,0);const auto step=std::polar(1.,.123);
        for(std::size_t i=0;i<repeated.size()/2;++i){repeated[2*i]=carrier.real();repeated[2*i+1]=carrier.imag();carrier*=step;}
        require(!estimateClock(repeated,nominal,outputRate,workers).locked,"A flat CW correlation was mistaken for a DTMB sampling clock");
        for(auto& item:repeated)item=value(random);
        require(!estimateClock(repeated,nominal,outputRate,workers).locked,"Uncorrelated noise was mistaken for a DTMB sampling clock");
        std::cout<<"Pinned resampler compatibility, independent fractional tone, absolute probe origins, chunk/worker invariance and failure recovery passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
