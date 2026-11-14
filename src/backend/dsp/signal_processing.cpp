#include "signalscope/backend/dsp/signal_processing.hpp"
#include <deque>
#include <limits>
#include <numbers>
#include <numeric>
#include <unordered_map>

namespace signalscope {
namespace {
constexpr double tau=2*std::numbers::pi;
std::uint64_t add(std::uint64_t a,std::uint64_t b) {
    if(b>UINT64_MAX-a)throw std::overflow_error("Signal clock addition overflow");
    return a+b;
}
std::uint64_t multiply(std::uint64_t a,std::uint64_t b) {
    if(b && a>UINT64_MAX/b)throw std::overflow_error("Signal clock multiplication overflow");
    return a*b;
}
// Rates are 32-bit, so the remainder product fits in uint64 even where
// uint128 is unavailable. No large coordinate is converted through double.
std::pair<std::uint64_t,std::uint32_t> scale(std::uint64_t n,std::uint32_t p,std::uint32_t q) {
    const auto remainder=std::uint64_t(n%q)*p;
    return {add(multiply(n/q,p),remainder/q),static_cast<std::uint32_t>(remainder%q)};
}
// Modular multiplication in Hz avoids losing the fractional turn by first
// converting a large sample index to floating point. The NCO is referenced to
// source_origin, independently of I/O chunking and gaps.
double phase(double hz,std::uint64_t index,std::uint32_t rate) {
    double value=0,step=std::remainder(hz,double(rate));
    while(index) {
        if(index&1)value=std::remainder(value+step,double(rate));
        step=std::remainder(step*2,double(rate));index>>=1;
    }
    return -tau*value/rate;
}
struct Run {
    std::size_t first_segment=0,last_segment=0;
    std::uint64_t first=0,end=0,first_output=0,end_output=0;
    bool discontinuous=false;
};
std::uint32_t filter_radius(const IqStreamParameters& stream,const ChannelExtractionOptions& options) {
    const auto input=stream.sample_rate,output=options.output_rate;
    if(!input || !output || input>100'000'000 || output>100'000'000 ||
       std::uint64_t(output)>std::uint64_t(input)*16 || std::uint64_t(input)>std::uint64_t(output)*4096 ||
       !std::isfinite(options.offset_hz) || !std::isfinite(options.passband_hz) || options.passband_hz<=0 ||
       !std::isfinite(options.transition_hz) || options.transition_hz<=0 ||
       options.passband_hz/2+options.transition_hz>std::min(input,output)/2. ||
       std::abs(options.offset_hz)+options.passband_hz/2+options.transition_hz>input/2. ||
       !std::isfinite(stream.center_frequency_hz+options.offset_hz) || stream.center_frequency_hz+options.offset_hz<=0)
        throw std::invalid_argument("Invalid complete channel extraction configuration");
    const auto requested=std::ceil(3.*input/options.transition_hz);
    if(requested>32767)throw std::length_error("Requested channel transition exceeds filter support budget");
    const auto radius=static_cast<std::uint32_t>(std::max(4.,requested));
    if(std::size_t(2*radius+1)*sizeof(std::complex<double>)>options.coefficient_payload_bytes)
        throw std::length_error("Channel coefficients exceed their payload budget");
    return radius;
}
Json channel_method(const IqStreamParameters& stream,const ChannelExtractionOptions& options,const SignalSampleClock& clock,std::uint32_t radius) {
    const auto divisor=std::gcd(stream.sample_rate,options.output_rate);
    return {{"id","rational-channel-fir"},{"version",1},{"offset_hz",options.offset_hz},
        {"passband_hz",options.passband_hz},{"transition_hz",options.transition_hz},
        {"input_rate_hz",stream.sample_rate},{"output_rate_hz",options.output_rate},{"up",options.output_rate/divisor},{"down",stream.sample_rate/divisor},
        {"filter","centered-blackman-sinc"},{"filter_taps",2*radius+1},
        {"causal_group_delay_source_samples",radius},{"timestamp_delay_compensated",true},
        {"boundary_policy","discard-incomplete-support"},{"mixer_phase_origin",std::to_string(clock.source_origin)},
        {"clock",{{"source_origin",std::to_string(clock.source_origin)},
            {"source_rate_hz",stream.sample_rate},{"rate_hz",options.output_rate}}}};
}
}
void SignalSampleClock::validate() const {
    if(!source_rate || !rate)throw std::invalid_argument("Invalid rational signal clock");
}
SourceSamplePosition SignalSampleClock::source_position(std::uint64_t index) const {
    validate();const auto [whole,fraction]=scale(index,source_rate,rate);
    if(!fraction)return {add(source_origin,whole),0,1};
    const auto divisor=std::gcd(fraction,rate);
    return {add(source_origin,whole),fraction/divisor,rate/divisor};
}
std::uint64_t SignalSampleClock::index_at_or_after(std::uint64_t sample) const {
    validate();if(sample<source_origin)throw std::out_of_range("Position precedes signal clock origin");
    const auto [whole,fraction]=scale(sample-source_origin,rate,source_rate);
    return fraction?add(whole,1):whole;
}
std::uint64_t SignalSampleClock::index_at_or_after(SourceSamplePosition position) const {
    validate();if(!position.denominator || position.numerator>=position.denominator || position.sample<source_origin)throw std::invalid_argument("Invalid rational source position");
    const auto [whole,remainder]=scale(position.sample-source_origin,rate,source_rate);
    const auto denominator=std::uint64_t(source_rate)*position.denominator;
    const auto first=std::uint64_t(remainder)*position.denominator,second=std::uint64_t(position.numerator)*rate;
    const auto fraction=second%denominator;const bool carry=first>=denominator-fraction;
    const auto residual=carry?first-(denominator-fraction):first+fraction;
    return add(add(add(whole,second/denominator),carry?1:0),residual?1:0);
}
std::uint64_t ProcessedSignal::sample_count() const {
    std::uint64_t count=0;for(const auto& segment:segments)count=add(count,segment.samples.size());return count;
}
void ProcessedSignal::validate() const {
    if(bool(source)==bool(recording))throw std::invalid_argument("Processed signal requires one original source reference");
    if(source)source->validate();
    if(recording) {
        recording->validate();
        if(!file_range.count || file_range.first>recording->storage.pairs || file_range.count>recording->storage.pairs-file_range.first)
            throw std::invalid_argument("Processed signal original file range is invalid");
    }
    clock.validate();
    if(clock.source_rate!=original_stream().sample_rate || !std::isfinite(center_frequency_hz) || center_frequency_hz<=0)
        throw std::invalid_argument("Processed signal clock or RF reference is inconsistent");
    const auto first_source=recording?*recording->source_index(file_range.first):source->segments.empty()?0:source->first_sample();
    const auto end_source=recording?add(*recording->source_index(file_range.first+file_range.count-1),1):source->segments.empty()?0:source->end_sample();
    std::optional<std::uint64_t> previous;
    for(const auto& segment:segments) {
        if(segment.samples.empty() || (previous && segment.first_sample<*previous))throw std::invalid_argument("Invalid processed signal segment");
        previous=add(segment.first_sample,segment.samples.size());
        const auto first=clock.source_position(segment.first_sample),last=clock.source_position(*previous-1);
        if(first.sample<first_source || last.sample>=end_source)
            throw std::invalid_argument("Processed signal lies outside its original source");
        for(auto value:segment.samples)if(!std::isfinite(value.real()) || !std::isfinite(value.imag()))throw std::invalid_argument("Non-finite processed signal value");
    }
}
const IqStreamParameters& ProcessedSignal::original_stream() const {
    if(recording)return recording->stream;
    if(source)return source->stream;
    throw std::invalid_argument("Missing original stream reference");
}
std::uint64_t ProcessedSignal::original_time_origin() const {
    if(recording)return recording->time_origin;
    if(source)return source->recording_path.empty() && !source->segments.empty()?source->first_sample():source->recording_origin;
    throw std::invalid_argument("Missing original time reference");
}
std::size_t ProcessedSignal::payload_bytes() const {
    std::size_t count=0;
    for(const auto& segment:segments) {
        const auto bytes=segment.samples.capacity()*sizeof(std::complex<float>);
        if(bytes>SIZE_MAX-count)throw std::overflow_error("Processed signal payload overflow");
        count+=bytes;
    }
    return count;
}
static ProcessedSignal extract_window(std::shared_ptr<const SignalWindow> source,
    const ChannelExtractionOptions& options,std::stop_token stop,std::optional<SignalSampleClock> fixed_clock={}) {
    if(!source)throw std::invalid_argument("Missing channel source");
    source->validate();
    const auto input=source->stream.sample_rate,output=options.output_rate;
    const auto radius=filter_radius(source->stream,options);
    const auto taps=2*radius+1;
    const auto coefficient_bytes=std::size_t(taps)*sizeof(std::complex<double>);
    ProcessedSignal result;result.source=std::move(source);
    result.clock=fixed_clock.value_or(SignalSampleClock{result.source->segments.empty()?result.source->recording_origin:result.source->first_sample(),input,output});
    result.clock.validate();if(result.clock.source_rate!=input || result.clock.rate!=output)throw std::invalid_argument("Channel clock and filter rates disagree");
    result.center_frequency_hz=result.source->stream.center_frequency_hz+options.offset_hz;
    result.method=channel_method(result.source->stream,options,result.clock,radius);
    if(stop.stop_requested())return result;
    std::vector<Run> runs;runs.reserve(result.source->segments.size());
    for(std::size_t i=0;i<result.source->segments.size();++i) {
        const auto& segment=result.source->segments[i];
        if(segment.samples.empty())continue;
        if(!runs.empty() && runs.back().end==segment.first_sample && !segment.discontinuous_before) {
            runs.back().end=add(segment.first_sample,segment.samples.size());runs.back().last_segment=i;
        }else runs.push_back({i,i,segment.first_sample,add(segment.first_sample,segment.samples.size()),0,0,segment.discontinuous_before});
    }
    std::uint64_t count=0;
    for(auto& run:runs) {
        if(run.end-run.first<=2*radius)continue;
        run.first_output=result.clock.index_at_or_after(add(run.first,radius));
        // floor(output position)+radius must remain strictly inside the run.
        run.end_output=result.clock.index_at_or_after(run.end-radius);
        count=add(count,run.end_output-run.first_output);
    }
    if(count>options.output_payload_bytes/sizeof(std::complex<float>))
        throw std::length_error("Channel output exceeds its payload budget");
    if(count>options.maximum_multiply_adds/taps)
        throw std::length_error("Channel extraction exceeds its computation budget");
    result.method["multiply_adds"]=std::to_string(count*taps);
    // Lazy rational phases: arbitrary co-prime rates must not allocate an
    // unbounded phase table. FIFO eviction changes cost, never coefficients.
    const auto phase_limit=std::min<std::size_t>(4096,options.coefficient_payload_bytes/coefficient_bytes);
    std::unordered_map<std::uint32_t,std::vector<std::complex<double>>> coefficients;
    std::deque<std::uint32_t> order;
    const auto cutoff=(options.passband_hz/2+options.transition_hz/2)/input;
    auto get_coefficients=[&](std::uint32_t remainder)->const std::vector<std::complex<double>>& {
        if(const auto it=coefficients.find(remainder);it!=coefficients.end())return it->second;
        if(coefficients.size()>=phase_limit){coefficients.erase(order.front());order.pop_front();}
        std::vector<std::complex<double>> values(taps);double sum=0;
        const auto fraction=double(remainder)/output;
        for(std::uint32_t i=0;i<taps;++i) {
            const auto k=double(i)-radius,x=k-fraction;
            if(std::abs(x)>radius)continue;
            const auto a=tau*cutoff*x;
            const auto window=.42+.5*std::cos(std::numbers::pi*x/radius)+.08*std::cos(tau*x/radius);
            const auto h=2*cutoff*(std::abs(a)<1e-15?1:std::sin(a)/a)*window;
            sum+=h;values[i]=h*std::polar(1.,-tau*options.offset_hz*k/input);
        }
        if(!std::isfinite(sum) || std::abs(sum)<1e-12)throw std::runtime_error("Invalid channel FIR normalization");
        for(auto& value:values)value/=sum;
        order.push_back(remainder);return coefficients.emplace(remainder,std::move(values)).first->second;
    };
    for(const auto& run:runs) {
        if(run.first_output==run.end_output)continue;
        ProcessedSignalSegment segment{run.first_output,{},run.discontinuous || !result.segments.empty()};
        segment.samples.resize(static_cast<std::size_t>(run.end_output-run.first_output));
        if(segment.samples.capacity()*sizeof(std::complex<float>)>options.output_payload_bytes-result.payload_bytes())
            throw std::length_error("Allocated channel output exceeds its payload budget");
        std::size_t first_segment=run.first_segment;
        for(auto index=run.first_output;index<run.end_output;++index) {
            if(((index-run.first_output)&255)==0 && stop.stop_requested()){result.segments.clear();return result;}
            const auto [whole,remainder]=scale(index,input,output);
            const auto center=add(result.clock.source_origin,whole),first=center-radius;
            while(first_segment<run.last_segment && first>=result.source->segments[first_segment].first_sample+result.source->segments[first_segment].samples.size())++first_segment;
            const auto& h=get_coefficients(remainder);std::complex<double> value{};
            auto position=first;std::size_t current=first_segment;
            for(std::uint32_t i=0;i<taps;++i,++position) {
                while(current<run.last_segment && position>=result.source->segments[current].first_sample+result.source->segments[current].samples.size())++current;
                const auto x=result.source->segments[current].samples[static_cast<std::size_t>(position-result.source->segments[current].first_sample)];
                value+=std::complex<double>(x)*h[i];
            }
            value*=std::polar(1.,phase(options.offset_hz,whole,input));
            segment.samples[static_cast<std::size_t>(index-run.first_output)]={static_cast<float>(value.real()),static_cast<float>(value.imag())};
        }
        result.segments.push_back(std::move(segment));
    }
    return result;
}
ProcessedSignal extract_signal_channel(std::shared_ptr<const SignalWindow> source,const ChannelExtractionOptions& options,std::stop_token stop) {
    return extract_window(std::move(source),options,stop);
}
ProcessedSignal extract_signal_source_channel(std::shared_ptr<const SignalSourceInfo> source,SignalSampleRange range,
    const ChannelExtractionOptions& options,const ChannelSourceBudget& budget,std::stop_token stop,ChannelSourceObserver observer) {
    if(!source)throw std::invalid_argument("Missing recorded channel source");
    source->validate();
    if(!range.count || range.first>source->storage.pairs || range.count>source->storage.pairs-range.first || !budget.central_samples || budget.central_samples>1048576)
        throw std::invalid_argument("Invalid recorded channel input range or block size");
    const auto radius=filter_radius(source->stream,options),taps=2*radius+1;
    if(range.count>budget.maximum_input_samples)throw std::length_error("Recorded channel input exceeds its work budget");
    if(budget.read_payload_bytes/sizeof(std::complex<float>)<=2*radius || budget.read_payload_bytes>budget.workspace_payload_bytes ||
       options.coefficient_payload_bytes>budget.workspace_payload_bytes-budget.read_payload_bytes)
        throw std::length_error("Recorded channel reads and coefficients exceed their workspace budget");
    const auto temporary_output=budget.workspace_payload_bytes-budget.read_payload_bytes-options.coefficient_payload_bytes;
    if(temporary_output<sizeof(std::complex<float>))throw std::length_error("Recorded channel has no temporary output budget");
    const auto metadata_unit=2*(sizeof(Run)+sizeof(ProcessedSignalSegment))+sizeof(SignalSegment);
    if(source->segments.size()>budget.metadata_payload_bytes/metadata_unit)throw std::length_error("Recorded channel maps exceed their metadata budget");
    const auto step=std::min<std::uint64_t>(budget.central_samples,budget.read_payload_bytes/sizeof(std::complex<float>)-2*radius);
    ProcessedSignal result;result.recording=std::move(source);result.file_range=range;
    result.clock={*result.recording->source_index(range.first),result.recording->stream.sample_rate,options.output_rate};
    result.center_frequency_hz=result.recording->stream.center_frequency_hz+options.offset_hz;
    result.method=channel_method(result.recording->stream,options,result.clock,radius);
    result.method["input"]={{"kind","recorded-source"},{"first_file_sample",std::to_string(range.first)},{"sample_count",std::to_string(range.count)}};
    if(stop.stop_requested())return result;
    std::vector<Run> runs;runs.reserve(result.recording->segments.size());const auto file_end=range.first+range.count;
    for(const auto& map:result.recording->segments) {
        const auto first=std::max(range.first,map.first_stored_sample),last=std::min(file_end,map.first_stored_sample+map.count);if(first>=last)continue;
        const auto begin=map.first_source_sample+first-map.first_stored_sample,end=begin+last-first;
        const auto discontinuous=first==map.first_stored_sample && map.discontinuous_before;
        if(!runs.empty() && runs.back().end==begin && !discontinuous)runs.back().end=end;
        else runs.push_back({0,0,begin,end,0,0,discontinuous});
    }
    std::uint64_t expected=0;
    for(auto& run:runs)if(run.end-run.first>2*radius) {
        run.first_output=result.clock.index_at_or_after(run.first+radius);run.end_output=result.clock.index_at_or_after(run.end-radius);
        expected=add(expected,run.end_output-run.first_output);
    }
    if(expected>options.output_payload_bytes/sizeof(std::complex<float>) || expected>options.maximum_multiply_adds/taps)
        throw std::length_error("Recorded channel output or computation exceeds its budget");
    result.segments.reserve(runs.size());
    for(const auto& run:runs)if(run.first_output<run.end_output) {
        ProcessedSignalSegment segment{run.first_output,std::vector<std::complex<float>>(static_cast<std::size_t>(run.end_output-run.first_output)),run.discontinuous || !result.segments.empty()};
        result.segments.push_back(std::move(segment));
    }
    if(result.payload_bytes()>options.output_payload_bytes || runs.capacity()*sizeof(Run)+result.segments.capacity()*sizeof(ProcessedSignalSegment)>budget.metadata_payload_bytes)
        throw std::length_error("Recorded channel allocation exceeds its budget");
    std::uint64_t written=0,operations=0,read_samples=0;std::size_t output_segment=0,output_index=0;
    for(auto first=range.first;first<file_end;) {
        if(stop.stop_requested()){result.segments.clear();return result;}
        const auto count=std::min<std::uint64_t>(step,file_end-first),read_first=first-std::min<std::uint64_t>(radius,first-range.first),read_end=first+count+std::min<std::uint64_t>(radius,file_end-first-count);
        if(read_end-read_first>budget.maximum_read_samples-read_samples)throw std::length_error("Recorded channel halo reads exceed their work budget");
        read_samples+=read_end-read_first;
        auto window=std::make_shared<SignalWindow>(read_signal_source(*result.recording,read_first,read_end-read_first,stop,budget.read_payload_bytes));
        if(stop.stop_requested()){result.segments.clear();return result;}
        if(window->processing!=result.recording->processing)throw std::invalid_argument("Recorded channel source must declare range-invariant processing provenance");
        if(observer)observer(*window,{first,count});
        auto local=options;local.output_payload_bytes=std::min(local.output_payload_bytes,temporary_output);local.maximum_multiply_adds=options.maximum_multiply_adds-operations;
        auto block=extract_window(std::move(window),local,stop,result.clock);
        if(stop.stop_requested()){result.segments.clear();return result;}
        operations=add(operations,std::stoull(block.method.at("multiply_adds").get<std::string>()));
        for(const auto& segment:block.segments)for(std::size_t i=0;i<segment.samples.size();++i) {
            const auto index=segment.first_sample+i;const auto position=result.clock.source_position(index);const auto stored=result.recording->stored_index(position.sample);
            if(!stored || *stored<first || *stored-first>=count)continue;
            while(output_segment<result.segments.size() && output_index==result.segments[output_segment].samples.size()){++output_segment;output_index=0;}
            if(output_segment>=result.segments.size() || index!=result.segments[output_segment].first_sample+output_index)throw std::runtime_error("Recorded channel tiles violated their declared clock or complete support");
            result.segments[output_segment].samples[output_index++]=segment.samples[i];++written;
        }
        first+=count;
    }
    if(written!=expected)throw std::runtime_error("Recorded channel tiles omitted complete output observations");
    result.method["multiply_adds"]=std::to_string(operations);result.method["read_payload_limit_bytes"]=budget.read_payload_bytes;
    result.method["read_samples_with_halo"]=std::to_string(read_samples);
    result.validate();return result;
}
}
