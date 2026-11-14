#include "signalscope/backend/dsp/waveform.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <iostream>
#include <limits>
#include <numbers>

using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void close(double value,double expected,double tolerance,const char* message) {
    if(!std::isfinite(value) || std::abs(value-expected)>tolerance)throw std::runtime_error(message);
}
template<class F> void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid waveform input was accepted");}
constexpr double tau=2*std::numbers::pi;
std::complex<float> sample(double amplitude,double phase) {const auto value=std::polar(amplitude,phase);return {static_cast<float>(value.real()),static_cast<float>(value.imag())};}
constexpr std::uint32_t rate=2'000'000;
constexpr std::uint64_t origin=(1ULL<<54)+71;
const IqStreamParameters stream{rate,470'500'000,SampleFormat::cf32_le};

template<class F> Bytes encode(SampleFormat format,std::size_t count,F formula) {
    const auto stride=bytes_per_pair(format);Bytes result(count*stride);
    for(std::size_t n=0;n<count;++n)write_iq_pair(result.data()+n*stride,format,formula(n));
    return result;
}
template<class F> SignalWindow make(std::size_t count,F formula) {
    SignalWindow w;w.stream=stream;SignalSegment s;s.first_sample=origin;
    for(std::size_t n=0;n<count;++n)s.samples.push_back(formula(n));
    w.segments.push_back(std::move(s));return w;
}
WaveformAnalysis analyze(SignalWindow window,const WaveformOptions& options={}) {
    return analyze_waveform(std::make_shared<SignalWindow>(std::move(window)),options);
}
void truthAndEncoding() {
    for(const auto& codec:sample_codecs) {
        const auto bytes=encode(codec.format,8192,[](auto n){const double t=double(n)/rate;return sample(.6,.27+tau*(-18000*t+.5*7'000'000*t*t));});
        SignalWindowBuffer buffer({2*1024*1024,1});auto s=stream;s.format=codec.format;
        buffer.append({bytes,origin,s,false});const auto result=analyze(buffer.snapshot());
        const double tolerance=codec.pair_bytes==2?8500:codec.pair_bytes==4?40:2;
        const auto& frequency=result.trace("frequency").values;
        const auto& amplitude=result.trace("amplitude").values;
        require(std::isnan(frequency.front()),"First sample incorrectly acquired a predecessor");
        for(std::size_t n=1;n<frequency.size();++n) {
            const auto expected=-18000+7'000'000*(double(n)-.5)/rate;
            close(frequency[n],expected,tolerance,"Chirp frequency differs from independent continuous-time truth");
            close(amplitude[n],.6,.007,"Normalized amplitude differs across sample codecs");
        }
        require(result.value_index(origin+8191)==8191 && !result.value_index(origin-1),"Large integer source positions were rounded");
        require(result.method.at("frequency_timestamp")=="right-hand-sample","Frequency interval convention is unspecified");
    }
    const auto negative=analyze(make(2048,[](auto n){return sample(.7,-tau*rate*.21*n/rate);}));
    for(std::size_t n=1;n<2048;++n)close(negative.trace("frequency").values[n],-.21*rate,20,"Negative frequency or phase wrapping is incorrect");
}
void chunksAndDiscontinuities() {
    const auto bytes=encode(SampleFormat::cf32_le,12345,[](auto n){return sample(.45,.4+tau*.037*n);});
    SignalWindowBuffer one({2*1024*1024,1}),split({2*1024*1024,1});one.append({bytes,origin,stream,false});
    const auto stride=bytes_per_pair(stream.format);
    for(std::size_t at=0;at<12345;) {
        const auto count=std::min<std::size_t>(12345-at,17+at%61);
        split.append({std::span(bytes).subspan(at*stride,count*stride),origin+at,stream,false});at+=count;
    }
    const auto whole=analyze(one.snapshot()),parts=analyze(split.snapshot());
    for(const auto& trace:whole.traces) {
        const auto& other=parts.trace(trace.id).values;
        for(std::size_t n=0;n<trace.values.size();++n)
            require((std::isnan(trace.values[n])&&std::isnan(other[n])) || trace.values[n]==other[n],"USB/read block boundaries changed mathematical results");
    }
    auto w=one.snapshot();auto suffix=w.segments.front();suffix.first_sample=origin+13000;suffix.samples.resize(30);w.segments.push_back(suffix);
    auto result=analyze(w);
    require(!result.value_index(origin+12500),"A missing sample was mapped to a real value");
    require(std::isnan(result.trace("frequency").values[12345]),"Instantaneous frequency crossed a known gap");
    close(result.trace("unwrapped_phase").values[12345],result.trace("phase").values[12345],1e-12,"Unwrapped phase retained an unsupported reference across a gap");
    w.segments.back().first_sample=origin+12345;w.segments.back().discontinuous_before=true;result=analyze(w);
    require(std::isnan(result.trace("frequency").values[12345]),"Unquantified contiguous discontinuity was ignored");
    const auto selected=select_signal_window(w,origin+12340,20);
    require(selected.sample_count()==20 && selected.segments.size()==2 && selected.segments.back().discontinuous_before,"Window selection destroyed a continuity boundary");
}
void amplitudeAndFilters() {
    const auto w=make(600,[](auto n){return sample(n>=200&&n<300?0:.5,tau*.1*n);});
    const auto result=analyze(w);
    for(std::size_t n=200;n<300;++n) {
        require(result.trace("i").values[n]==0 && result.trace("amplitude").values[n]==0,"Low amplitude raw values were hidden");
        require(std::isnan(result.trace("phase").values[n]) && std::isnan(result.trace("frequency").values[n]),"Zero amplitude manufactured a phase or frequency");
    }
    require(std::isnan(result.trace("frequency").values[300]) && std::isfinite(result.trace("frequency").values[301]),"Frequency resumed without a valid adjacent pair");
    const auto step=analyze(make(500,[](auto n){return sample(.5,tau*(n<250?.03*n:.03*250+.13*(n-250)));}),{1e-8,0,9});
    close(step.trace("frequency").values[251],.13*rate,10,"Raw frequency jump was smoothed away");
    require(step.trace("smoothed_frequency").values[251]<step.trace("frequency").values[251]-1000,"Filtered and raw estimates are indistinguishable at a step");
}
void budgetsAndInputValidation() {
    const auto bytes=encode(SampleFormat::cf32_le,80,[](auto n){return sample(.5,tau*.02*n);});
    SignalWindowBuffer buffer({128*sizeof(std::complex<float>),1});
    for(unsigned n=0;n<1000;++n)buffer.append({bytes,origin+80ULL*n,stream,false});
    const auto s=buffer.status();
    require(s.accepted_samples==80000 && s.expired_samples==79920 && s.retained_payload_bytes<=1024,"Retained buffer is unbounded or expiration is misclassified");
    buffer.append({bytes,origin+80100,stream,true});require(buffer.status().known_missing_samples==100,"Known input gap length was lost");
    buffer.append({bytes,origin+80180,stream,true});require(buffer.status().unquantified_discontinuities==1,"Unquantified source boundary was not counted separately");
    require(buffer.snapshot().sample_count()==80,"Preview retained samples outside its payload budget");
    const auto before=buffer.status().accepted_samples;
    rejects([&]{buffer.append({std::span(bytes).first(7),origin+80260,stream,false});});
    rejects([&]{buffer.append({bytes,origin+80000,stream,false});});
    auto changed=stream;changed.sample_rate/=2;rejects([&]{buffer.append({bytes,origin+80260,changed,false});});
    require(buffer.status().accepted_samples==before,"Rejected input changed accepted sample accounting");
    rejects([&]{analyze(make(100,[](auto){return std::complex<float>(.5f,0);}),{1e-8,.05,9,100});});
    auto invalid=make(2,[](auto){return std::complex<float>(1,0);});invalid.segments.front().samples[0]={std::numeric_limits<float>::quiet_NaN(),0};rejects([&]{analyze(invalid);});
    invalid=make(2,[](auto){return std::complex<float>(1,0);});invalid.segments.front().first_sample=std::numeric_limits<std::uint64_t>::max();rejects([&]{analyze(invalid);});
    std::stop_source cancellation;cancellation.request_stop();const auto canceled=analyze_waveform(std::make_shared<SignalWindow>(buffer.snapshot()),{},cancellation.get_token());
    require(canceled.traces.empty(),"Canceled analysis published an apparently complete trace set");
}
void recordings() {
    const auto root=std::filesystem::temp_directory_path()/path_from_utf8("amc-waveform-波形-📡-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        Config config;config.rate=rate;
        for(const auto& codec:sample_codecs) {
            const auto path=root/sample_datatype(codec.format);
            const auto bytes=encode(codec.format,4096,[](auto n){return sample(.6,.12+tau*.031*n);});
            Recorder recorder(path,config,"synthetic-waveform-truth",codec.format);recorder.append(bytes);recorder.finish("complete",{{"stream_first_sample",origin}});
            const auto metadata=path/"capture.sigmf-meta";
            const auto window=read_signal_window(metadata,123,321);
            require(window.recording_path==std::filesystem::absolute(metadata) && window.first_sample()==origin+123 && window.sample_count()==321,"Recording range lost its native path or source mapping");
            const auto full=read_signal_window(metadata,0,4096);
            const auto cut=select_signal_window(full,origin+123,321);
            const auto a=analyze(window),b=analyze(cut);
            for(const auto& trace:a.traces)for(std::size_t n=0;n<trace.values.size();++n) {
                const auto v=b.trace(trace.id).values[n];require((std::isnan(v)&&std::isnan(trace.values[n])) || v==trace.values[n],"Native recording selection differs from an independently selected source interval");
            }
            rejects([&]{read_signal_window(metadata,4000,321);});rejects([&]{read_signal_window(metadata,0,4096,{},8);});
            std::stop_source stop;stop.request_stop();require(read_signal_window(metadata,0,4096,stop.get_token()).segments.empty(),"Canceled read retained partial samples");
        }
    }catch(...){std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);
}
}
int main() {
    try {truthAndEncoding();chunksAndDiscontinuities();amplitudeAndFilters();budgetsAndInputValidation();recordings();
        std::cout<<"Independent waveform truth, codec normalization, large source coordinates, chunk invariance, gaps, validity, filters, budgets, cancellation and Unicode recordings passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
