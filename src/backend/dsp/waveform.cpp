#include "signalscope/backend/dsp/waveform.hpp"
#include <array>
#include <limits>
#include <numbers>

namespace signalscope {
namespace {constexpr std::size_t trace_count=8;}
std::size_t waveform_sample_capacity(const WaveformOptions& options) noexcept {return options.output_payload_bytes/(trace_count*sizeof(double));}
const AnalysisTrace& WaveformAnalysis::trace(const std::string& id) const {
    const auto found=std::find_if(traces.begin(),traces.end(),[&](const auto& t){return t.id==id;});
    if(found==traces.end())throw std::invalid_argument("Unknown waveform trace");
    return *found;
}
std::optional<std::size_t> WaveformAnalysis::value_index(std::uint64_t sample) const {
    const auto found=std::upper_bound(spans.begin(),spans.end(),sample,[](auto n,const auto& span){return n<span.first_sample;});
    if(found==spans.begin())return {};
    const auto& span=*std::prev(found);const auto offset=sample-span.first_sample;
    if(offset>=span.count)return {};
    return span.first_value+static_cast<std::size_t>(offset);
}
std::size_t WaveformAnalysis::payload_bytes() const {
    return analysis_traces_payload_bytes(traces);
}
const std::vector<AnalysisTraceSpan>& analysis_trace_spans(const WaveformAnalysis& analysis,const AnalysisTrace& trace){return trace.mapping?*trace.mapping:analysis.spans;}
std::optional<std::size_t> analysis_trace_value_index(const WaveformAnalysis& analysis,const AnalysisTrace& trace,std::uint64_t sample) {
    const auto& spans=analysis_trace_spans(analysis,trace);const auto found=std::upper_bound(spans.begin(),spans.end(),sample,[](auto n,const auto& span){return n<span.first_sample;});
    if(found==spans.begin())return {};
    const auto& span=*std::prev(found);const auto offset=sample-span.first_sample;
    return offset<span.count?std::optional<std::size_t>(span.first_value+static_cast<std::size_t>(offset)):std::nullopt;
}
void validate_analysis_trace(const WaveformAnalysis& analysis,const AnalysisTrace& trace) {
    const auto count=analysis.sample_count();if(!trace.mapping){if(trace.values.size()!=count)throw std::invalid_argument("Trace values do not match their parent axis");return;}
    std::size_t expected=0;std::uint64_t previous=0;
    for(const auto& mapped:*trace.mapping) {
        if(!mapped.count || mapped.first_value!=expected || mapped.count>trace.values.size()-expected || mapped.first_sample>UINT64_MAX-mapped.count || (expected && mapped.first_sample<previous))throw std::invalid_argument("Invalid packed trace mapping");
        const auto end=mapped.first_sample+mapped.count;auto parent=std::upper_bound(analysis.spans.begin(),analysis.spans.end(),mapped.first_sample,[](auto n,const auto& span){return n<span.first_sample;});
        if(parent==analysis.spans.begin())throw std::invalid_argument("Packed trace precedes its parent observations");
        --parent;
        for(auto position=mapped.first_sample;position<end;) {
            if(parent==analysis.spans.end() || position<parent->first_sample || position-parent->first_sample>=parent->count ||
                (position==parent->first_sample && parent->discontinuous_before && (position!=mapped.first_sample || !mapped.discontinuous_before)))throw std::invalid_argument("Packed trace crosses absent or discontinuous observations");
            position=std::min(end,parent->first_sample+parent->count);if(position<end)++parent;
        }
        expected+=mapped.count;previous=end;
    }
    if(expected!=trace.values.size())throw std::invalid_argument("Packed trace mapping omits values");
}
std::size_t analysis_traces_payload_bytes(const std::vector<AnalysisTrace>& traces) {
    std::size_t bytes=0;const auto add=[&](std::size_t count,std::size_t width){if(count>(SIZE_MAX-bytes)/width)throw std::length_error("Trace payload size overflow");bytes+=count*width;};
    for(std::size_t i=0;i<traces.size();++i) {
        const auto& trace=traces[i];add(trace.values.capacity(),sizeof(double));
        if(trace.mapping){bool seen=false;for(std::size_t j=0;j<i;++j)if(traces[j].mapping.get()==trace.mapping.get()){seen=true;break;}if(!seen)add(trace.mapping->capacity(),sizeof(AnalysisTraceSpan));}
    }return bytes;
}
std::uint64_t WaveformAnalysis::sample_count() const {return processed?processed->sample_count():source->sample_count();}
std::uint32_t WaveformAnalysis::sample_rate() const {return processed?processed->clock.rate:source->stream.sample_rate;}
std::uint64_t WaveformAnalysis::first_sample() const {
    if(processed){if(processed->segments.empty())throw std::out_of_range("Empty processed analysis");return processed->segments.front().first_sample;}
    return source->first_sample();
}
std::uint64_t WaveformAnalysis::end_sample() const {
    if(processed){if(processed->segments.empty())throw std::out_of_range("Empty processed analysis");const auto& last=processed->segments.back();return last.first_sample+last.samples.size();}
    return source->end_sample();
}
std::uint64_t WaveformAnalysis::time_origin() const {return processed?0:source->recording_path.empty()?source->first_sample():source->recording_origin;}
double WaveformAnalysis::time_offset_seconds() const {
    if(!processed)return 0;
    const auto origin=processed->original_time_origin();
    const auto offset=processed->clock.source_origin;
    return (offset>=origin?double(offset-origin):-double(origin-offset))/processed->original_stream().sample_rate;
}
SourceSamplePosition WaveformAnalysis::source_position(std::uint64_t sample) const {return processed?processed->clock.source_position(sample):SourceSamplePosition{sample,0,1};}
namespace {
struct InputSegment {std::uint64_t first_sample;std::span<const std::complex<float>> samples;bool discontinuous_before;};
WaveformAnalysis analyze(std::shared_ptr<const SignalWindow> source,std::shared_ptr<const ProcessedSignal> processed,
    const WaveformOptions& options,std::stop_token stop) {
    if(!source && !processed)throw std::invalid_argument("Missing signal input provenance");
    if(source)source->validate();
    if(processed)processed->validate();
    if(!std::isfinite(options.minimum_amplitude) || options.minimum_amplitude<0 ||
        !std::isfinite(options.relative_amplitude_floor) || options.relative_amplitude_floor<0 || options.relative_amplitude_floor>1 ||
        !options.frequency_smoothing_samples || options.frequency_smoothing_samples>100001)
        throw std::invalid_argument("Invalid waveform analysis options");
    const auto size=processed?processed->sample_count():source->sample_count();
    if(size>waveform_sample_capacity(options))
        throw std::length_error("Waveform traces exceed their payload budget");
    WaveformAnalysis result;result.source=std::move(source);result.processed=std::move(processed);
    std::vector<InputSegment> segments;
    const auto append=[&](const auto& input){for(const auto& segment:input)segments.push_back({segment.first_sample,segment.samples,segment.discontinuous_before});};
    if(result.processed)append(result.processed->segments);else append(result.source->segments);
    result.method={{"id","adjacent-phase-difference"},{"version",1},
        {"minimum_amplitude",options.minimum_amplitude},{"relative_amplitude_floor",options.relative_amplitude_floor},
        {"smoothing_samples",options.frequency_smoothing_samples},{"smoothing_alignment","causal"},
        {"frequency_reference_hz",result.processed?result.processed->center_frequency_hz:result.source->stream.center_frequency_hz},
        {"frequency_timestamp","right-hand-sample"},{"phase_reference","independent-continuous-segment"}};
    if(result.processed)result.method["processing"]=result.processed->method;
    if(stop.stop_requested())return result;
    constexpr std::array ids{"i","q","amplitude","phase","unwrapped_phase","phase_difference","frequency","smoothed_frequency"};
    constexpr std::array units{"1","1","1","rad","rad","rad","Hz","Hz"};
    const auto invalid=std::numeric_limits<double>::quiet_NaN();
    for(std::size_t n=0;n<trace_count;++n)
        result.traces.push_back({ids[n],std::string("trace.")+ids[n],units[n],std::vector<double>(static_cast<std::size_t>(size),invalid)});
    if(result.payload_bytes()>options.output_payload_bytes)throw std::length_error("Allocated waveform traces exceed their payload budget");
    std::size_t index=0;double maximum=0;
    for(const auto& segment:segments) {
        result.spans.push_back({segment.first_sample,index,segment.samples.size(),segment.discontinuous_before});
        for(const auto value:segment.samples) {
            if((index&4095)==0 && stop.stop_requested()){result.traces.clear();result.spans.clear();return result;}
            result.traces[0].values[index]=value.real();result.traces[1].values[index]=value.imag();
            const auto magnitude=std::hypot(double(value.real()),double(value.imag()));
            result.traces[2].values[index]=magnitude;maximum=std::max(maximum,magnitude);++index;
        }
    }
    const auto floor=std::max(options.minimum_amplitude,maximum*options.relative_amplitude_floor);
    result.method["applied_amplitude_floor"]=floor;
    std::optional<std::uint64_t> next;bool previous_valid=false;
    double previous_phase=0,unwrapped=0,sum=0;std::deque<double> smoothing;
    index=0;
    for(const auto& segment:segments) {
        if(!next || segment.first_sample!=*next || segment.discontinuous_before){previous_valid=false;smoothing.clear();sum=0;}
        for(const auto value:segment.samples) {
            if((index&4095)==0 && stop.stop_requested()){result.traces.clear();result.spans.clear();return result;}
            const auto magnitude=result.traces[2].values[index];
            const bool valid=magnitude>floor;
            if(valid) {
                const auto phase=std::atan2(double(value.imag()),double(value.real()));
                result.traces[3].values[index]=phase;
                if(previous_valid) {
                    const auto delta=std::remainder(phase-previous_phase,2*std::numbers::pi);
                    const auto frequency=delta*result.sample_rate()/(2*std::numbers::pi);
                    unwrapped+=delta;result.traces[5].values[index]=delta;result.traces[6].values[index]=frequency;
                    smoothing.push_back(frequency);sum+=frequency;
                    if(smoothing.size()>options.frequency_smoothing_samples){sum-=smoothing.front();smoothing.pop_front();}
                    result.traces[7].values[index]=sum/smoothing.size();
                }else {unwrapped=phase;smoothing.clear();sum=0;}
                result.traces[4].values[index]=unwrapped;previous_phase=phase;
            }else {smoothing.clear();sum=0;}
            previous_valid=valid;++index;
        }
        next=segment.first_sample+segment.samples.size();
    }
    return result;
}
}
WaveformAnalysis analyze_waveform(std::shared_ptr<const SignalWindow> source,const WaveformOptions& options,std::stop_token stop) {return analyze(std::move(source),{},options,stop);}
WaveformAnalysis analyze_waveform(std::shared_ptr<const ProcessedSignal> source,const WaveformOptions& options,std::stop_token stop) {
    if(!source)throw std::invalid_argument("Missing processed waveform source");
    const auto original=source->source;return analyze(original,std::move(source),options,stop);
}
}
