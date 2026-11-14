#include "signalscope/backend/dsp/window_research.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <iostream>
#include <numbers>
#include <thread>

using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr auto origin=(1ULL<<54)+93;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"Invalid recorded channel operation was accepted");}
std::complex<float> tone(double a,double phase){const auto value=std::polar(a,phase);return {float(value.real()),float(value.imag())};}
void longFileTruth(const std::filesystem::path& root) {
    Config config;config.rate=2000000;config.frequency=433000000;const auto format=SampleFormat::cf32_le;const auto stride=bytes_per_pair(format);constexpr std::uint64_t total=1200000;
    Recorder recording(root/path_from_utf8("长录制 📡"),config,"independent-channel-truth",format);Bytes bytes(8192*stride);
    for(std::uint64_t first=0;first<total;first+=8192) {
        const auto count=std::min<std::uint64_t>(8192,total-first);
        for(std::size_t i=0;i<count;++i){const auto n=first+i;const auto value=tone(.61,.23+tau*((303*(n%2000))%2000)/2000.)+tone(.3,-.31+tau*(3*(n%10)%10)/10.);write_iq_pair(bytes.data()+i*stride,format,value);}
        recording.append(std::span(bytes).first(count*stride));
    }
    recording.finish("complete",{{"stream_first_sample",origin}});auto source=std::make_shared<SignalSourceInfo>(inspect_signal_source(root/path_from_utf8("长录制 📡")/"capture.sigmf-meta"));
    std::uint64_t largest_read=0,observed=0,next=17;const auto original=source->read;
    source->read=[&](const auto& info,auto range,auto stop,auto maximum){largest_read=std::max(largest_read,range.count);require(range.count*sizeof(std::complex<float>)<=1024*1024,"Long source reader was asked for an unbounded normalized window");return original(info,range,stop,maximum);};
    const SignalSampleRange range{17,1100000};const ChannelExtractionOptions options{300000,20000,10000,50000};
    auto result=extract_signal_source_channel(source,range,options,{}, {},[&](const auto& window,auto central){
        require(central.first==next && window.sample_count()<20000,"Haloed reads lost central ownership or retained the whole file");next+=central.count;observed+=central.count;
    });
    require(!result.source && result.recording==source && result.file_range==range && result.clock.source_origin==origin+17 && observed==range.count && next==range.first+range.count && largest_read<20000,"Long-file result invented a loaded raw window or lost source ownership");
    require(total*stride>8*1024*1024 && result.payload_bytes()<256*1024,"Long-file fixture did not exceed the raw preview budget or output was not narrowed");
    double maximum=0;const auto initial=.23+tau*((303*17)%2000)/2000.;
    for(const auto& segment:result.segments)for(std::size_t i=0;i<segment.samples.size();++i) {
        const auto t=double(segment.first_sample+i)/50000;
        maximum=std::max(maximum,std::abs(std::complex<double>(segment.samples[i])-std::polar(.61,initial+tau*3000*t)));
    }
    require(maximum<.0003,"Streamed channel differs from independent continuous-time isolated truth");
    const auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(std::make_shared<ProcessedSignal>(std::move(result))));
    require(!analysis->source && analysis->sample_rate()==50000 && std::abs(analysis->time_offset_seconds()-17./2000000)<1e-15,"File-backed trace protocol lost its source reference or whole-recording time offset");
    const auto stats=research_signal_window(analysis,{analysis->first_sample(),4096});require(std::abs(stats.statistics.mean_frequency_hz-3000)<.1 && std::abs(stats.statistics.mean_power-.3721)<.0005,"Window research required a fictitious RAM source or used the wrong streamed clock");
    std::stop_source stop;unsigned blocks=0;const auto canceled=extract_signal_source_channel(source,range,options,{},stop.get_token(),[&](const auto&,auto){if(++blocks==3)stop.request_stop();});
    require(blocks==3 && canceled.segments.empty(),"Canceled recorded extraction published partially filled observations");
    auto budget=ChannelSourceBudget{};budget.maximum_input_samples=10;rejects([&]{extract_signal_source_channel(source,range,options,budget);});
    budget={};budget.maximum_read_samples=8192;rejects([&]{extract_signal_source_channel(source,range,options,budget);});
    budget={};budget.read_payload_bytes=8;rejects([&]{extract_signal_source_channel(source,range,options,budget);});
    budget={};budget.workspace_payload_bytes=8;rejects([&]{extract_signal_source_channel(source,range,options,budget);});
    auto invalid=options;invalid.output_payload_bytes=8;rejects([&]{extract_signal_source_channel(source,range,invalid);});
    auto changing=std::make_shared<SignalSourceInfo>(*source);changing->read=[original](const auto& info,auto selected,auto token,auto maximum){auto result=original(info,selected,token,maximum);result.processing.push_back({{"undeclared_range_method",selected.first}});return result;};
    rejects([&]{extract_signal_source_channel(changing,range,options);});
}
void chunksAndGaps(const std::filesystem::path& root) {
    for(const auto& codec:sample_codecs) {
        SignalWindow window;window.stream={96000,434000000,codec.format};window.source_id="independent.segmented-file";window.processing=Json::array({{{"independent_future_field",19}}});
        const auto segment=[&](std::uint64_t first,std::size_t count,bool gap){SignalSegment s{first,{},gap};Bytes bytes(codec.pair_bytes);
            for(std::size_t i=0;i<count;++i){write_iq_pair(bytes.data(),codec.format,tone(.64,.17+tau*((first-origin+i)%48)/48.));s.samples.push_back(read_iq_pair(bytes.data(),codec.format));}return s;};
        window.segments={segment(origin,2048,false),segment(origin+2048,2048,true),segment(origin+8192,4096,false)};
        auto source=std::make_shared<SignalSourceInfo>(inspect_signal_source(write_signal_archive(root/sample_datatype(codec.format),window)));
        const SignalSampleRange range{17,window.sample_count()-34};const auto full=std::make_shared<SignalWindow>(read_signal_source(*source,range.first,range.count));const ChannelExtractionOptions options{1000,16000,8000,32001};const auto expected=extract_signal_channel(full,options);
        for(const auto size:{31u,127u,4096u}) {
            ChannelSourceBudget budget;budget.central_samples=size;const auto result=extract_signal_source_channel(source,range,options,budget);
            require(result.segments.size()==expected.segments.size() && result.sample_count()==expected.sample_count(),"File tiles bridged gaps or omitted complete support");
            for(std::size_t i=0;i<result.segments.size();++i)require(result.segments[i].first_sample==expected.segments[i].first_sample && result.segments[i].discontinuous_before==expected.segments[i].discontinuous_before && result.segments[i].samples==expected.segments[i].samples,"Changing file block size changed coefficients, phase, samples or discontinuities");
        }
        require(source->source_index(0)==origin && source->source_index(4096)==origin+8192 && !source->source_index(source->storage.pairs),"Inverse source map invented a stored position outside the file");
    }
}
}
int main(){const auto root=std::filesystem::temp_directory_path()/path_from_utf8("amc-file-channel-信道-📡-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));bool owned=false;
    try{owned=std::filesystem::create_directory(root);require(owned,"Independent channel fixture already exists");longFileTruth(root);chunksAndGaps(root);std::filesystem::remove_all(root);std::cout<<"Long-file bounded reads, independent channel truth, exact clock, all codecs, gaps, chunk invariance and cancellation passed\n";return 0;}catch(const std::exception& error){if(owned)std::filesystem::remove_all(root);std::cerr<<error.what()<<'\n';return 1;}}
