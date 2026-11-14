// Private native CF32-LE symbol sampler. No user filename or public I/Q codec
// is interpreted here: the receiving adapter owns that boundary.
#include "symbol_sampler.hpp"
#include "clock_estimator.hpp"
#include "dtmb/core.hpp"
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace signalscope::television::native;
namespace {
static_assert(std::endian::native==std::endian::little,"Native DTMB helpers require the declared little-endian platform");
constexpr std::size_t maximumInput=512*1024*1024;
struct Options {std::uint32_t input=10000000,output=7560000;std::uint64_t clock=0;unsigned workers=4,phase=0,pn=0;std::size_t chunk=65536,budget=maximumInput;bool automatic=false;};
std::uint64_t integer(const char* argument) {
    const std::string_view text(argument);std::uint64_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size())throw std::invalid_argument("resample.arguments");return value;
}
Options options(int argc,char** argv) {
    Options result;unsigned positional=0;
    for(int i=1;i<argc;++i) {
        const std::string key=argv[i];
        if(key=="--auto-timing"){result.automatic=true;continue;}
        if(key=="-"){if(++positional>2)throw std::invalid_argument("resample.arguments");continue;}
        if(i+1==argc)throw std::invalid_argument("resample.arguments");
        if(key=="--input-format" || key=="--output-format"){if(std::string_view(argv[++i])!="cf32")throw std::invalid_argument("resample.private_format");continue;}
        const auto value=integer(argv[++i]);
        if(key=="--input-rate" && value>=7560000 && value<=100000000)result.input=value;
        else if(key=="--output-rate" && value==7560000)result.output=value;
        else if(key=="--workers" && value>=1 && value<=64)result.workers=value;
        else if(key=="--timing-phase" && value<SymbolSampler::phaseCount)result.phase=value;
        else if(key=="--clock-rate-millihz" && value>=1 && value<=1000000000000ULL)result.clock=value;
        else if(key=="--pn-symbols" && (value==0 || value==420 || value==595 || value==945))result.pn=value;
        else if(key=="--chunk-samples" && value>=1 && value<=1024*1024)result.chunk=value;
        else if(key=="--input-budget-bytes" && value>=8 && value<=maximumInput)result.budget=value;
        else throw std::invalid_argument("resample.arguments");
    }
    if(!result.clock)result.clock=std::uint64_t(result.input)*1000;
    const auto nominal=std::uint64_t(result.input)*1000;
    if(result.clock<nominal-nominal/1000 || result.clock>nominal+nominal/1000)throw std::invalid_argument("resample.clock_range");return result;
}
std::size_t read(std::span<float> buffer) {
    const auto bytes=std::fread(buffer.data(),1,buffer.size_bytes(),stdin);
    if(std::ferror(stdin))throw std::runtime_error("resample.read");
    if(bytes%8)throw std::runtime_error("resample.partial_sample");
    const auto values=bytes/sizeof(float);for(std::size_t i=0;i<values;++i)if(!std::isfinite(buffer[i]))throw std::invalid_argument("resample.nonfinite");
    return values;
}
void write(std::span<const float> data) {
    if(std::fwrite(data.data(),sizeof(float),data.size(),stdout)!=data.size())throw std::runtime_error("resample.write");
    if(std::fflush(stdout))throw std::runtime_error("resample.write");
}
std::vector<float> convert(std::span<const float> source,const Options& options,unsigned phase,Workers* workers) {
    if(options.clock==std::uint64_t(options.output)*1000 && !phase)return {source.begin(),source.end()};
    SymbolSampler sampler(options.input,options.output,phase,workers,0,0,options.clock);std::vector<float> output;
    output.reserve(static_cast<std::size_t>(std::ceil(double(source.size())*options.output*1000/options.clock)));
    for(std::size_t first=0;first<source.size();first+=131072)sampler.process(source.subspan(first,std::min<std::size_t>(131072,source.size()-first)),output);
    sampler.finish(output);return output;
}
struct Window {dtmb::core::PnMode pn=dtmb::core::PnMode::pn945;std::size_t first=0,size=0;float score=0;};
Window findWindow(std::span<const float> symbols,unsigned forced) {
    Window best;const auto pairs=symbols.size()/2;
    for(const auto pn:{dtmb::core::PnMode::pn945,dtmb::core::PnMode::pn420,dtmb::core::PnMode::pn595}) {
        if(forced && unsigned(pn)!=forced)continue;const auto frame=dtmb::core::pn_definition(pn).frame_symbols();
        const auto window=frame*33;if(pairs<window)continue;
        for(const auto first:{std::size_t(0),(pairs-window)/2,pairs-window}) {
            const auto result=dtmb::core::acquire_pn_cf32(pn,symbols.subspan(first*2,window*2),{16,.35F,1});
            if(result.hit_count>=2 && result.mean_metric>best.score)best={pn,first,window,result.mean_metric};
        }
    }
    return best;
}
unsigned choosePhase(std::span<const float> source,std::span<const float> baseline,const Options& options,Workers& workers) {
    const auto window=findWindow(baseline,options.pn);if(window.score<.35F){std::cerr<<"timing_score="<<window.score<<'\n';return 0;}
    // Each candidate needs only the same short PN window. Origins preserve the
    // absolute phase of the full prefix, including for late-arriving signals.
    std::array<std::vector<float>,SymbolSampler::phaseCount> candidates;
    const auto denominator=std::uint64_t(options.output)*1000;
    const auto radius=std::size_t(std::ceil(8.*options.clock/denominator))+4;
    const auto center=std::uint64_t(window.first)*options.clock/denominator;
    const auto first=center>radius?center-radius:0;
    const auto last=std::min<std::uint64_t>(source.size()/2,
        (std::uint64_t(window.first+window.size+2)*options.clock+denominator-1)/denominator+radius);
    workers.run(SymbolSampler::phaseCount-1,[&](auto begin,auto end){
        for(auto item=begin;item<end;++item) {
            const auto phase=unsigned(item+1);SymbolSampler sampler(options.input,options.output,phase,nullptr,first,window.first,options.clock);
            auto& output=candidates[phase];output.reserve((window.size+radius+8)*2);
            sampler.process(source.subspan(first*2,(last-first)*2),output);sampler.finish(output);
            if(output.size()>window.size*2)output.resize(window.size*2);
        }
    },1);
    unsigned selected=0;float score=window.score;
    for(unsigned phase=1;phase<SymbolSampler::phaseCount;++phase)if(candidates[phase].size()>=window.size*2) {
        const auto result=dtmb::core::acquire_pn_cf32(window.pn,candidates[phase],{16,.35F,1});
        if(result.hit_count>=2 && result.mean_metric>score+1e-6F){score=result.mean_metric;selected=phase;}
    }
    std::cerr<<"timing_pn="<<unsigned(window.pn)<<"\ntiming_window="<<window.first<<"\ntiming_baseline_score="<<window.score
        <<"\ntiming_score="<<score<<"\ntiming_candidate_payload_bytes="<<(SymbolSampler::phaseCount-1)*window.size*8<<'\n';
    return selected;
}
}
int main(int argc,char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    try {
        auto settings=options(argc,argv);Workers workers(settings.workers);std::vector<float> input(settings.chunk*2);std::cerr<<std::setprecision(12);
        std::uint64_t inputPairs=0,outputPairs=0;unsigned phase=settings.phase;
        if(settings.automatic) {
            std::vector<float> source;source.reserve(settings.budget/sizeof(float));
            while(const auto values=read(input)) {
                if(source.size()*sizeof(float)+values*sizeof(float)>settings.budget)throw std::runtime_error("resample.input_budget");
                source.insert(source.end(),input.begin(),input.begin()+std::ptrdiff_t(values));
            }
            if(source.empty())throw std::runtime_error("resample.no_input");inputPairs=source.size()/2;
            const auto clock=estimateClock(source,settings.input,settings.output,workers);settings.clock=clock.rateMilliHz;
            std::cerr<<"clock_locked="<<(clock.locked?"true":"false")<<"\nclock_error_ppm="<<clock.ppm<<"\nclock_score="<<clock.score<<"\nclock_contrast="<<clock.contrast<<'\n';
            auto output=convert(source,settings,0,&workers);phase=choosePhase(source,output,settings,workers);
            if(phase)output=convert(source,settings,phase,&workers);outputPairs=output.size()/2;write(output);
        }else {
            SymbolSampler sampler(settings.input,settings.output,phase,&workers,0,0,settings.clock);std::vector<float> output;
            while(const auto values=read(input)) {
                inputPairs+=values/2;output.clear();
                if(settings.clock==std::uint64_t(settings.output)*1000 && !phase)output.assign(input.begin(),input.begin()+std::ptrdiff_t(values));
                else sampler.process(std::span(input).first(values),output);
                outputPairs+=output.size()/2;write(output);
            }
            output.clear();if(settings.clock!=std::uint64_t(settings.output)*1000 || phase)sampler.finish(output);outputPairs+=output.size()/2;write(output);
        }
        std::cerr<<"resample_protocol=3\nclock_rate_millihz="<<settings.clock<<"\ntiming_phase="<<phase<<"\ntiming_phase_count="<<SymbolSampler::phaseCount
            <<"\ninput_samples="<<inputPairs<<"\noutput_samples="<<outputPairs<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
