#pragma once

#include "signalscope/backend/iq/signal_source.hpp"
#include <memory>

namespace signalscope {
inline constexpr unsigned processedSignalProtocolVersion=2;

// An exact position on the original source clock. The fraction is in [0,1).
// In particular, a processed point is not rounded to an original observation.
struct SourceSamplePosition {
    std::uint64_t sample=0;
    std::uint32_t numerator=0,denominator=1;
    bool operator==(const SourceSamplePosition&) const=default;
};
struct SignalSampleClock {
    std::uint64_t source_origin=0;
    std::uint32_t source_rate=0,rate=0;
    bool operator==(const SignalSampleClock&) const=default;
    void validate() const;
    SourceSamplePosition source_position(std::uint64_t index) const;
    std::uint64_t index_at_or_after(std::uint64_t source_sample) const;
    std::uint64_t index_at_or_after(SourceSamplePosition) const;
};

struct ChannelExtractionOptions {
    double offset_hz=0;
    double passband_hz=0; // full two-sided passband, excluding transition
    double transition_hz=0; // width on each side
    std::uint32_t output_rate=0;
    std::size_t output_payload_bytes=16*1024*1024;
    std::size_t coefficient_payload_bytes=2*1024*1024;
    std::uint64_t maximum_multiply_adds=250'000'000;
};
struct ProcessedSignalSegment {
    std::uint64_t first_sample=0; // processed clock, not original source clock
    std::vector<std::complex<float>> samples;
    bool discontinuous_before=false;
};
struct ProcessedSignal {
    // Either immutable RAM observations, or a file reference and its exact
    // stored-sample range. A long-file result must not invent a fully loaded
    // raw window or present a small preview as the whole original source.
    std::shared_ptr<const SignalWindow> source;
    std::shared_ptr<const SignalSourceInfo> recording;
    SignalSampleRange file_range;
    SignalSampleClock clock;
    double center_frequency_hz=0;
    std::vector<ProcessedSignalSegment> segments;
    Json method;
    void validate() const;
    std::uint64_t sample_count() const;
    std::size_t payload_bytes() const;
    const IqStreamParameters& original_stream() const;
    std::uint64_t original_time_origin() const;
};
// Offline zero-phase, centered Blackman FIR, evaluated at the rational output
// phases. No inserted zeros, circular wrap, edge extrapolation or gap filling.
// Only points with complete observed filter support are returned. Adjacent
// continuous I/O segments form one support; an unknown discontinuity resets it.
// This has a distinct clock/protocol from raw SignalWindow and cannot silently
// be saved as a raw integer-clock archive.
ProcessedSignal extract_signal_channel(std::shared_ptr<const SignalWindow>,
    const ChannelExtractionOptions&,std::stop_token stop={});
struct ChannelSourceBudget {
    std::size_t read_payload_bytes=1024*1024,workspace_payload_bytes=4*1024*1024,metadata_payload_bytes=8*1024*1024;
    std::uint32_t central_samples=8192;
    std::uint64_t maximum_input_samples=100'000'000,maximum_read_samples=256'000'000;
};
// The observer borrows a haloed read plus the nonoverlapping central FILE
// interval. It can stream source identity/evidence without retaining raw data.
using ChannelSourceObserver=std::function<void(const SignalWindow&,SignalSampleRange)>;
ProcessedSignal extract_signal_source_channel(std::shared_ptr<const SignalSourceInfo>,SignalSampleRange,
    const ChannelExtractionOptions&,const ChannelSourceBudget& budget={},std::stop_token stop={},ChannelSourceObserver observer={});
}
