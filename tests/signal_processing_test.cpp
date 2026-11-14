#include "signalscope/backend/dsp/signal_processing.hpp"
#include "signalscope/backend/dsp/waveform.hpp"
#include <iostream>
#include <limits>
#include <numbers>

using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::uint64_t origin=(1ULL<<54)+39;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected,double tolerance,const char* message) {
    require(std::isfinite(value) && std::abs(value-expected)<=tolerance,message);
}
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"Invalid channel operation was accepted");}
std::complex<float> tone(double amplitude,double phase) {
    const auto value=std::polar(amplitude,phase);return {float(value.real()),float(value.imag())};
}
template<class F>std::shared_ptr<SignalWindow> make(std::uint32_t rate,std::size_t count,F formula) {
    auto source=std::make_shared<SignalWindow>();source->stream={rate,433'000'000,SampleFormat::cf32_le};source->source_id="independent-continuous-truth";
    SignalSegment segment{origin,{},false};segment.samples.reserve(count);
    for(std::size_t i=0;i<count;++i)segment.samples.push_back(formula(i));source->segments.push_back(std::move(segment));return source;
}
void exactClock() {
    const SignalSampleClock clock{origin,44100,16000};
    require(clock.source_position(1)==SourceSamplePosition{origin+2,121,160},"Fractional source position was rounded");
    require(clock.source_position(16000)==SourceSamplePosition{origin+44100,0,1},"Rational source clock drifted over one second");
    require(clock.index_at_or_after(origin+3)==2 && clock.index_at_or_after(origin+44100)==16000,"Integer source selection does not map to the correct output boundary");
    require(clock.index_at_or_after(SourceSamplePosition{origin+2,121,160})==1 && clock.index_at_or_after(SourceSamplePosition{origin+2,122,160})==2,"Exact fractional source cursor was rounded during inverse mapping");
    require(SignalSampleClock{0,1,UINT32_MAX}.index_at_or_after(SourceSamplePosition{0,UINT32_MAX-1,UINT32_MAX})==UINT32_MAX-1,"Large rational products overflowed the inverse clock");
    const SignalSampleClock unity{0,1,1};require(unity.source_position(UINT64_MAX).sample==UINT64_MAX,"Native clock limit lost precision");
    rejects([&]{clock.source_position(UINT64_MAX);});rejects([&]{clock.index_at_or_after(origin-1);});
    rejects([]{SignalSampleClock{0,0,1}.source_position(0);});
    rejects([]{SignalSampleClock{0,1,2}.index_at_or_after(UINT64_MAX);});
}
void isolationAndPhase() {
    constexpr unsigned rate=2'000'000;
    auto source=make(rate,32768,[](auto n){const auto t=double(n)/rate;return tone(.6,.37+tau*330000*t)+tone(.3,.12+tau*600000*t);});
    const ChannelExtractionOptions options{300000,120000,60000,400000};
    const auto result=extract_signal_channel(source,options);
    require(result.source==source && result.clock.source_origin==origin && result.segments.size()==1,"Channel extraction lost its original immutable source");
    require(result.center_frequency_hz==433300000 && result.clock.rate==400000,"Channel output retained the wideband RF reference");
    double maximum=0;
    for(const auto& segment:result.segments)for(std::size_t n=0;n<segment.samples.size();++n) {
        const auto index=segment.first_sample+n;
        const auto expected=std::polar(.6,.37+tau*30000*double(index)/options.output_rate);
        maximum=std::max(maximum,std::abs(std::complex<double>(segment.samples[n])-expected));
        const auto position=result.clock.source_position(index);
        require(position.sample>=origin+100 && position.sample<origin+32768-100,"Incomplete filter boundary was represented as an observation");
    }
    near(maximum,0,.0002,"Channel output differs from independent isolated continuous-time truth");
    require(result.method.at("timestamp_delay_compensated")==true && result.method.at("causal_group_delay_source_samples")==100,"Filter delay or timestamp convention was omitted");
    require(source->sample_count()==32768 && source->processing.empty(),"Extraction modified original observations or history");
}
void fractionalRates() {
    for(const auto output:{16000u,48000u}) {
        constexpr unsigned rate=44100;constexpr double offset=-3500,frequency=1379.125;
        auto source=make(rate,8192,[](auto n){return tone(.71,-.43+tau*(offset+frequency)*double(n)/rate);});
        auto result=extract_signal_channel(source,{offset,6000,2500,output});
        double maximum=0;for(const auto& segment:result.segments)for(std::size_t n=0;n<segment.samples.size();++n) {
            const auto t=double(segment.first_sample+n)/output;
            maximum=std::max(maximum,std::abs(std::complex<double>(segment.samples[n])-std::polar(.71,-.43+tau*frequency*t)));
        }
        near(maximum,0,.0002,"Rational polyphase interpolation has the wrong phase, rate or gain");
        require(result.method.at("up")==160 && result.method.at("down")== (output==16000?441:147),"Reduced rational rate was not recorded");
        auto processed=std::make_shared<ProcessedSignal>(std::move(result));const auto analysis=analyze_waveform(processed);
        require(analysis.source==source && analysis.processed==processed && analysis.sample_rate()==output && analysis.sample_count()==processed->sample_count(),"Processed analysis flattened its source clock into raw coordinates");
        require(analysis.source_position(analysis.first_sample())==processed->clock.source_position(processed->segments.front().first_sample),"Processed trace lost its fractional source mapping");
        const auto& frequencies=analysis.trace("frequency").values;
        for(std::size_t n=1;n<frequencies.size();++n)near(frequencies[n],frequency,1,"Frequency analysis used the original sample rate after resampling");
    }
    auto source=make(44100,8192,[](auto){return std::complex<float>{.75f,-.25f};});
    const auto dc=extract_signal_channel(source,{0,6000,2500,16000});
    for(const auto& segment:dc.segments)for(auto x:segment.samples)near(std::abs(x-std::complex<float>(.75f,-.25f)),0,1e-6,"Fractional phases changed DC normalization");
}
void segmentationAndGaps() {
    auto source=make(96000,4096,[](auto n){return tone(.6,.27+tau*4000*double(n)/96000);});
    auto split=std::make_shared<SignalWindow>(*source);const auto samples=source->segments.front().samples;split->segments.clear();
    for(std::size_t n=0;n<samples.size();) {
        const auto count=std::min<std::size_t>(37,samples.size()-n);
        split->segments.push_back({origin+n,{samples.begin()+n,samples.begin()+n+count},false});n+=count;
    }
    const ChannelExtractionOptions options{1000,16000,8000,32000};
    const auto a=extract_signal_channel(source,options),b=extract_signal_channel(split,options);
    require(a.segments.size()==1 && b.segments.size()==1 && a.segments[0].first_sample==b.segments[0].first_sample && a.segments[0].samples==b.segments[0].samples,"Continuous I/O chunks changed filter observations or phase");
    split->segments.clear();split->segments.push_back({origin,{samples.begin(),samples.begin()+1024},false});
    split->segments.push_back({origin+1024,{samples.begin()+1024,samples.begin()+2048},true});
    split->segments.push_back({origin+3072,{samples.begin()+3072,samples.end()},false});
    const auto gapped=extract_signal_channel(split,options);require(gapped.segments.size()==3,"Known or unknown gaps were interpolated away");
    const auto radius=gapped.method.at("causal_group_delay_source_samples").get<unsigned>();
    for(std::size_t i=0;i<3;++i) {
        const auto& output=gapped.segments[i];const auto& input=split->segments[i];
        require(gapped.clock.source_position(output.first_sample).sample>=input.first_sample+radius && gapped.clock.source_position(output.first_sample+output.samples.size()-1).sample<input.first_sample+input.samples.size()-radius,"FIR support crossed an unobserved boundary");
        if(i)require(output.discontinuous_before,"Derived segment concealed its discontinuity");
    }
    // An extremely sparse source must keep a precise mixing phase. These
    // tones have period 8, allowing a separate integer-modulo truth oracle.
    auto sparse=make(80000,512,[](auto n){return tone(.5,tau*(n%8)/8.);});
    constexpr auto next=UINT64_MAX-2048;
    SignalSegment segment{next,{},true};for(std::uint64_t n=0;n<512;++n)segment.samples.push_back(tone(.5,tau*((next-origin+n)%8)/8.));sparse->segments.push_back(std::move(segment));
    const auto result=extract_signal_channel(sparse,{10000,8000,8000,40000});
    require(result.segments.size()==2,"Large source-clock gap removed a valid region");
    for(const auto& run:result.segments)for(auto x:run.samples)near(std::abs(x-std::complex<float>(.5f,0)),0,1e-5,"Large sample coordinate corrupted the NCO phase");
}
void limitsAndCancellation() {
    auto source=make(96000,4096,[](auto){return std::complex<float>{1,0};});
    ChannelExtractionOptions options{0,16000,8000,32000};
    auto invalid=options;invalid.passband_hz=30000;rejects([&]{extract_signal_channel(source,invalid);});
    invalid=options;invalid.offset_hz=47000;rejects([&]{extract_signal_channel(source,invalid);});
    invalid=options;invalid.transition_hz=0;rejects([&]{extract_signal_channel(source,invalid);});
    invalid=options;invalid.transition_hz=1;rejects([&]{extract_signal_channel(source,invalid);});
    invalid=options;invalid.coefficient_payload_bytes=8;rejects([&]{extract_signal_channel(source,invalid);});
    invalid=options;invalid.output_payload_bytes=8;rejects([&]{extract_signal_channel(source,invalid);});
    invalid=options;invalid.maximum_multiply_adds=16;rejects([&]{extract_signal_channel(source,invalid);});
    std::stop_source stop;stop.request_stop();require(extract_signal_channel(source,options,stop.get_token()).segments.empty(),"Canceled extraction published partial observations");
    auto small=make(96000,12,[](auto){return std::complex<float>{1,0};});require(extract_signal_channel(small,options).segments.empty(),"Too-short observation manufactured filter support");
}
}
int main() {try{exactClock();isolationAndPhase();fractionalRates();segmentationAndGaps();limitsAndCancellation();std::cout<<"Exact channel clocks, independent signal truth, gaps and bounded extraction passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
