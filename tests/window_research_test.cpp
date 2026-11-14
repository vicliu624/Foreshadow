#include "signalscope/backend/dsp/window_research.hpp"
#include <iostream>
#include <numbers>
#include <numeric>

using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::uint64_t origin=(1ULL<<54)+91;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance,const char* message){require(std::isfinite(a) && std::abs(a-b)<=tolerance,message);}
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"Invalid window research operation was accepted");}
std::complex<float> tone(double amplitude,double turns){const auto x=std::polar(amplitude,tau*turns);return {float(x.real()),float(x.imag())};}
template<class F>std::shared_ptr<WaveformAnalysis> fixture(std::uint32_t rate,std::size_t count,F formula) {
    auto source=std::make_shared<SignalWindow>();source->stream={rate,433920000,SampleFormat::cf32_le};source->source_id="independent-window-truth";
    SignalSegment segment{origin,{},false};for(std::size_t i=0;i<count;++i)segment.samples.push_back(formula(i));source->segments.push_back(std::move(segment));
    return std::make_shared<WaveformAnalysis>(analyze_waveform(source));
}
SignalSampleRange whole(const WaveformAnalysis& a){return {a.first_sample(),a.end_sample()-a.first_sample()};}
void packedFeatureMapping() {
    auto analysis=fixture(48000,512,[](auto n){return tone(.5,double(n%16)/16);});
    auto found=std::find_if(analysis->traces.begin(),analysis->traces.end(),[](const auto& trace){return trace.id=="frequency";});require(found!=analysis->traces.end(),"Frequency feature is missing");
    found->values.assign(32,123);found->mapping=std::make_shared<std::vector<AnalysisTraceSpan>>(std::initializer_list<AnalysisTraceSpan>{{origin+100,0,32,false}});
    WindowResearchOptions options;options.fft_size=256;options.hop_samples=256;const auto result=research_signal_window(analysis,whole(*analysis),options);
    require(result.statistics.observed_samples==512 && result.statistics.valid_frequency_samples==32,"Sparse feature points were confused with full observed I/Q");near(result.statistics.mean_frequency_hz,123,1e-12,"Frequency statistics used complete-parent indices for a packed feature");
    require(std::accumulate(result.frequency_histogram.counts.begin(),result.frequency_histogram.counts.end(),std::uint64_t(0))==32,"Packed frequency histogram invented missing features");
}
void spectralPowerTruth() {
    const auto analysis=fixture(65536,4096,[](auto n){return tone(.5,double(n%16)/16)+tone(.25,3.*(n%16)/16);});
    for(const auto window:{ResearchWindow::rectangular,ResearchWindow::hann,ResearchWindow::hamming,ResearchWindow::blackman}) {
        WindowResearchOptions options;options.fft_size=256;options.hop_samples=128;options.window=window;options.correlation_lags=32;
        const auto result=research_signal_window(analysis,whole(*analysis),options);
        near(result.statistics.mean_power,.3125,1e-7,"Time-domain mean power is not normalized complex power");
        near(std::abs(result.statistics.mean_iq),0,1e-7,"Window mean introduced a fictitious DC component");
        require(result.frame_first_samples.size()==31 && result.frame_first_samples.front()==origin && result.frame_first_samples.back()==origin+3840,"STFT support, hop or source clock is wrong");
        const auto sum=std::accumulate(result.average_spectral_power.begin(),result.average_spectral_power.end(),0.);
        near(sum*256,.3125,2e-7,"PSD integral violates independent power truth or doubles complex IQ power");
        options.scaling=SpectralScaling::power_per_bin;const auto per_bin=research_signal_window(analysis,whole(*analysis),options);
        near(std::accumulate(per_bin.average_spectral_power.begin(),per_bin.average_spectral_power.end(),0.),.3125,2e-7,"Per-bin power does not integrate to normalized power");
        for(std::size_t i=0;i<256;++i)near(per_bin.average_spectral_power[i],result.average_spectral_power[i]*256,1e-10,"Density and per-bin power have inconsistent physical units");
        if(window==ResearchWindow::rectangular){near(per_bin.average_spectral_power[128+16],.25,1e-7,"Positive tone spectrum is at the wrong RF offset");near(per_bin.average_spectral_power[128+48],.0625,1e-7,"Second signal power or frequency is wrong");}
        if(window==ResearchWindow::hann){near(result.method.at("equivalent_noise_bandwidth_hz").get<double>(),384,1e-8,"Periodic Hann ENBW differs from its analytic value");near(result.method.at("window_coherent_gain").get<double>(),.5,1e-12,"Hann coherent gain is wrong");}
    }
    WindowResearchOptions options;options.fft_size=512;options.hop_samples=512;options.window=ResearchWindow::rectangular;
    const auto larger=research_signal_window(analysis,whole(*analysis),options);near(std::accumulate(larger.average_spectral_power.begin(),larger.average_spectral_power.end(),0.)*128,.3125,1e-7,"Changing FFT length changed integrated physical power");
    const auto negative=fixture(65536,512,[](auto n){return tone(.6,-double(n%8)/8);});options.fft_size=256;options.hop_samples=256;
    const auto result=research_signal_window(negative,whole(*negative),options);near(result.average_spectral_power[96]*256,.36,1e-7,"Negative-frequency complex signal was folded into a real one-sided spectrum");
}
void impulseAndStatistics() {
    const auto impulse=fixture(48000,256,[](auto n){return std::complex<float>{n==128?1.f:0.f,0};});
    WindowResearchOptions options;options.fft_size=256;options.hop_samples=256;options.window=ResearchWindow::rectangular;
    const auto rectangular=research_signal_window(impulse,whole(*impulse),options);
    for(auto p:rectangular.average_spectral_power)near(p,1./(48000*256),1e-14,"Impulse PSD differs from analytic white spectrum");
    near(rectangular.statistics.mean_power,1./256,1e-14,"Impulse time power is wrong");
    require(rectangular.statistics.valid_frequency_samples==0 && std::accumulate(rectangular.frequency_histogram.counts.begin(),rectangular.frequency_histogram.counts.end(),std::uint64_t(0))==0,"Low amplitude or absent predecessors generated fictitious frequency observations");
    options.window=ResearchWindow::hann;const auto hann=research_signal_window(impulse,whole(*impulse),options);
    near(std::accumulate(hann.average_spectral_power.begin(),hann.average_spectral_power.end(),0.)*48000/256,8./(3*256),1e-10,"Windowed PSD integral is not the correct weighted energy");
    near(hann.statistics.mean_power,rectangular.statistics.mean_power,1e-15,"Changing spectral window changed the selected-sample mean power");
    const auto carrier=fixture(48000,1024,[](auto n){return tone(.7,double(n%32)/32);});
    const SignalSampleRange selected{origin+100,512};const auto result=research_signal_window(carrier,selected,options);
    near(result.statistics.mean_frequency_hz,1500,.01,"Window frequency statistics used display buckets or the wrong sample rate");
    require(result.statistics.valid_frequency_samples==511,"The selection borrowed a derivative predecessor from outside its support");
    require(std::accumulate(result.frequency_histogram.counts.begin(),result.frequency_histogram.counts.end(),std::uint64_t(0))==511,"Histogram lost or invented valid estimates");
    for(const auto& point:result.correlation){require(point.pairs==512-point.lag_samples,"Correlation pair normalization is wrong");const auto expected=std::polar(.49,tau*point.lag_samples/32);near(std::abs(point.value-expected),0,1e-7,"Complex correlation has the wrong lag direction, gain or phase");}
}
void gapsAndChunking() {
    auto original=fixture(48000,1536,[](auto){return std::complex<float>{.5f,0};});
    auto source=std::make_shared<SignalWindow>(*original->source);const auto values=source->segments.front().samples;
    source->segments={{origin,{values.begin(),values.begin()+512},false},{origin+512,{values.begin()+512,values.begin()+1024},true},{origin+2048,{values.begin()+1024,values.end()},false}};
    const auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));WindowResearchOptions options;options.fft_size=256;options.hop_samples=128;options.correlation_lags=32;
    const auto result=research_signal_window(analysis,whole(*analysis),options);
    require(result.statistics.observed_samples==1536 && result.statistics.missing_clock_positions==1024 && result.frame_first_samples.size()==9,"Gap positions were zero-filled or used as complete FFT observations");
    for(const auto& point:result.correlation){require(point.pairs==3*(512-point.lag_samples),"Correlation crossed a known or unknown source discontinuity");near(std::abs(point.value),.25,1e-12,"Gap handling changed observed correlation power");}
    require(result.statistics.valid_frequency_samples==1533,"Adjacent unknown discontinuity supplied a fictitious predecessor");
    const auto empty=research_signal_window(analysis,{origin+1200,300},options);require(empty.statistics.observed_samples==0 && empty.statistics.missing_clock_positions==300 && empty.frame_first_samples.empty(),"A wholly missing selection invented measured samples");
    auto chunked=std::make_shared<SignalWindow>(*original->source);chunked->segments.clear();for(std::size_t n=0;n<values.size();n+=37){const auto count=std::min<std::size_t>(37,values.size()-n);chunked->segments.push_back({origin+n,{values.begin()+n,values.begin()+n+count},false});}
    const auto a=research_signal_window(original,whole(*original),options),b=research_signal_window(std::make_shared<WaveformAnalysis>(analyze_waveform(chunked)),whole(*original),options);
    require(a.spectral_power==b.spectral_power && a.frame_first_samples==b.frame_first_samples && a.correlation[1].pairs==b.correlation[1].pairs,"Continuous I/O chunking changed STFT or correlation");
}
void processedAndLimits() {
    auto source=fixture(48000,4096,[](auto n){return tone(.6,double(n%12)/12);});const auto processed=std::make_shared<ProcessedSignal>(extract_signal_channel(source->source,{3000,6000,2000,16000}));
    const auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(processed));WindowResearchOptions options;options.fft_size=256;options.hop_samples=128;
    const auto result=research_signal_window(analysis,whole(*analysis),options);near(result.statistics.mean_frequency_hz,1000,.1,"Research statistics ignored the processed clock or RF shift");
    require(result.method.at("source_clock").at("source_origin")==std::to_string(origin),"Research result lost its source clock mapping");
    auto invalid=options;invalid.fft_size=1000;rejects([&]{research_signal_window(analysis,whole(*analysis),invalid);});
    invalid=options;invalid.result_payload_bytes=1;rejects([&]{research_signal_window(analysis,whole(*analysis),invalid);});
    invalid=options;invalid.workspace_payload_bytes=1;rejects([&]{research_signal_window(analysis,whole(*analysis),invalid);});
    invalid=options;invalid.maximum_correlation_pairs=1;rejects([&]{research_signal_window(analysis,whole(*analysis),invalid);});
    invalid=options;invalid.maximum_frames=1;rejects([&]{research_signal_window(analysis,whole(*analysis),invalid);});
    invalid=options;invalid.window=static_cast<ResearchWindow>(999);rejects([&]{research_signal_window(analysis,whole(*analysis),invalid);});
    rejects([&]{research_signal_window(analysis,{UINT64_MAX,2},options);});
    std::stop_source stop;stop.request_stop();require(research_signal_window(analysis,whole(*analysis),options,stop.get_token()).spectral_power.empty(),"Canceled research published a partial matrix");
}
}
int main(){try{spectralPowerTruth();impulseAndStatistics();gapsAndChunking();processedAndLimits();packedFeatureMapping();std::cout<<"Independent power/PSD, window energy, complex frequency, correlation, histogram, clock, gaps and budget truth passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
