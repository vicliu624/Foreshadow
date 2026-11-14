#include "signalscope/backend/iq/signal_window.hpp"
#include "signalscope/backend/iq/signal_source.hpp"
#include <limits>

namespace signalscope {
namespace {
std::uint64_t checked_end(std::uint64_t first,std::uint64_t count) {
    if(count>std::numeric_limits<std::uint64_t>::max()-first)
        throw std::invalid_argument("Signal sample interval overflows the source clock");
    return first+count;
}
void validate_stream(const IqStreamParameters& stream) {
    if(!stream.sample_rate || !std::isfinite(stream.center_frequency_hz) || stream.center_frequency_hz<=0)
        throw std::invalid_argument("Invalid signal window stream parameters");
    (void)sample_codec(stream.format);
}
}

void SignalWindow::validate() const {
    validate_stream(stream);
    if(!processing.is_array())throw std::invalid_argument("Signal processing history must be an array");
    std::optional<std::uint64_t> previous;
    std::uint64_t total=0;
    for(const auto& segment:segments) {
        if(segment.samples.empty())throw std::invalid_argument("Empty signal segment");
        if(previous && segment.first_sample<*previous)
            throw std::invalid_argument("Signal segments overlap or are out of order");
        previous=checked_end(segment.first_sample,segment.samples.size());
        total=checked_end(total,segment.samples.size());
        for(const auto sample:segment.samples)
            if(!std::isfinite(sample.real()) || !std::isfinite(sample.imag()))
                throw std::invalid_argument("Non-finite normalized signal sample");
    }
}
std::uint64_t SignalWindow::sample_count() const {
    std::uint64_t count=0;
    for(const auto& segment:segments)count=checked_end(count,segment.samples.size());
    return count;
}
std::size_t SignalWindow::payload_bytes() const {
    const auto count=sample_count();
    if(count>std::numeric_limits<std::size_t>::max()/sizeof(std::complex<float>))
        throw std::length_error("Signal payload exceeds addressable memory");
    return static_cast<std::size_t>(count)*sizeof(std::complex<float>);
}
std::size_t SignalWindow::allocated_payload_bytes() const {
    std::size_t bytes=0;
    for(const auto& segment:segments) {
        if(segment.samples.capacity()>(SIZE_MAX-bytes)/sizeof(std::complex<float>))throw std::length_error("Allocated signal payload exceeds addressable memory");
        bytes+=segment.samples.capacity()*sizeof(std::complex<float>);
    }
    return bytes;
}
std::uint64_t SignalWindow::first_sample() const {
    if(segments.empty())throw std::invalid_argument("Signal window is empty");
    return segments.front().first_sample;
}
std::uint64_t SignalWindow::end_sample() const {
    if(segments.empty())throw std::invalid_argument("Signal window is empty");
    return checked_end(segments.back().first_sample,segments.back().samples.size());
}
std::optional<SignalSampleAddress> locate_signal_sample(const SignalWindow& window,std::uint64_t sample) {
    const auto it=std::upper_bound(window.segments.begin(),window.segments.end(),sample,
        [](auto n,const auto& segment){return n<segment.first_sample;});
    if(it==window.segments.begin())return {};
    const auto& segment=*std::prev(it);
    const auto offset=sample-segment.first_sample;
    if(offset>=segment.samples.size())return {};
    return SignalSampleAddress{static_cast<std::size_t>(std::prev(it)-window.segments.begin()),static_cast<std::size_t>(offset)};
}
SignalWindow select_signal_window(const SignalWindow& source,std::uint64_t first,std::uint64_t count) {
    source.validate();
    const auto end=checked_end(first,count);
    SignalWindow result;
    result.stream=source.stream;result.recording_path=source.recording_path;result.source_id=source.source_id;
    result.recording_origin=source.recording_origin;result.processing=source.processing;
    for(const auto& segment:source.segments) {
        const auto begin=std::max(first,segment.first_sample);
        const auto finish=std::min(end,checked_end(segment.first_sample,segment.samples.size()));
        if(begin>=finish)continue;
        const auto at=static_cast<std::size_t>(begin-segment.first_sample);
        const auto size=static_cast<std::size_t>(finish-begin);
        result.segments.push_back({begin,{segment.samples.begin()+at,segment.samples.begin()+at+size},
            begin==segment.first_sample && segment.discontinuous_before});
    }
    return result;
}
SignalWindow read_signal_window(const std::filesystem::path& metadata,std::uint64_t first,
    std::uint64_t count,std::stop_token stop,std::size_t maximum_payload_bytes) {
    return read_signal_source(inspect_signal_source(metadata),first,count,stop,maximum_payload_bytes);
}

SignalWindowBuffer::SignalWindowBuffer(SignalWindowBudget budget):budget_(budget) {
    if(budget.payload_bytes<sizeof(std::complex<float>) || !std::isfinite(budget.seconds) || budget.seconds<=0 || budget.seconds>60 || !budget.maximum_segments || budget.maximum_segments>65536)
        throw std::invalid_argument("Invalid signal window retention budget");
}
void SignalWindowBuffer::clear(){segments_.clear();stream_.reset();next_sample_.reset();status_={};}
void SignalWindowBuffer::append(const IqBlockView& block) {
    validate_stream(block.stream);
    const auto stride=bytes_per_pair(block.stream.format);
    if(block.bytes.empty() || block.bytes.size()%stride)
        throw std::invalid_argument("Incomplete or empty I/Q block");
    if(stream_ && (stream_->sample_rate!=block.stream.sample_rate || stream_->format!=block.stream.format ||
        stream_->center_frequency_hz!=block.stream.center_frequency_hz))
        throw std::invalid_argument("Signal stream changed without resetting its window buffer");
    const auto pairs=block.bytes.size()/stride;
    const auto next=checked_end(block.first_sample,pairs);
    if(next_sample_ && block.first_sample<*next_sample_)
        throw std::invalid_argument("Out-of-order I/Q block");
    const auto time_limit=static_cast<std::uint64_t>(std::floor(block.stream.sample_rate*budget_.seconds));
    const auto maximum=std::min<std::uint64_t>(budget_.payload_bytes/sizeof(std::complex<float>),time_limit);
    if(!maximum)throw std::invalid_argument("Signal time budget cannot retain one sample");
    const auto keep=static_cast<std::size_t>(std::min<std::uint64_t>(pairs,maximum));
    const auto skipped=pairs-keep;
    SignalSegment segment;segment.first_sample=block.first_sample+skipped;
    segment.discontinuous_before=block.discontinuous;
    segment.samples.reserve(keep);
    for(auto at=skipped*stride;at<block.bytes.size();at+=stride) {
        const auto value=read_iq_pair(block.bytes.data()+at,block.stream.format);
        if(!std::isfinite(value.real()) || !std::isfinite(value.imag()))throw std::invalid_argument("Non-finite live signal sample");
        segment.samples.push_back(value);
    }
    const auto cost=segment.samples.capacity()*sizeof(std::complex<float>);
    if(cost>budget_.payload_bytes)throw std::length_error("Allocated signal segment exceeds the retention budget");
    while(!segments_.empty() && (status_.retained_payload_bytes>budget_.payload_bytes-cost ||
        next-segments_.front().first_sample>maximum || segments_.size()>=budget_.maximum_segments)) {
        const auto& oldest=segments_.front();
        status_.expired_samples+=oldest.samples.size();
        status_.retained_payload_bytes-=oldest.samples.capacity()*sizeof(std::complex<float>);
        segments_.pop_front();
    }
    if(next_sample_) {
        if(block.first_sample>*next_sample_)status_.known_missing_samples+=block.first_sample-*next_sample_;
        else if(block.discontinuous)++status_.unquantified_discontinuities;
    }
    stream_=block.stream;next_sample_=next;
    status_.accepted_samples+=pairs;status_.accepted_input_bytes+=block.bytes.size();status_.expired_samples+=skipped;
    status_.retained_payload_bytes+=cost;
    segments_.push_back(std::move(segment));
}
SignalWindow SignalWindowBuffer::snapshot(const std::string& source_id) const {
    SignalWindow result;result.source_id=source_id;
    if(stream_)result.stream=*stream_;
    result.segments.assign(segments_.begin(),segments_.end());
    return result;
}
}
