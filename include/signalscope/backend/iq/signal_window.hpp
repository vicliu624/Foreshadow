#pragma once

#include "signalscope/backend/iq/stream.hpp"
#include <deque>
#include <optional>
#include <stop_token>

namespace signalscope {
inline constexpr unsigned signalWindowProtocolVersion=1;
struct SignalSampleRange {
    std::uint64_t first=0,count=0;
    bool operator==(const SignalSampleRange&) const=default;
};

// first_sample always refers to the source clock, never to a display bucket.
// Adjacent segments may be continuous; a discontinuity flag also preserves
// boundaries for which the source cannot quantify the number of missing pairs.
struct SignalSegment {
    std::uint64_t first_sample=0;
    std::vector<std::complex<float>> samples;
    bool discontinuous_before=false;
};

struct SignalWindow {
    IqStreamParameters stream{};
    std::filesystem::path recording_path;
    std::string source_id;
    std::uint64_t recording_origin=0;
    std::vector<SignalSegment> segments;
    Json processing=Json::array();

    void validate() const;
    std::uint64_t sample_count() const;
    std::size_t payload_bytes() const;
    std::size_t allocated_payload_bytes() const;
    std::uint64_t first_sample() const;
    std::uint64_t end_sample() const;
};

struct SignalSampleAddress {
    std::size_t segment=0,index=0;
};
std::optional<SignalSampleAddress> locate_signal_sample(const SignalWindow&,std::uint64_t sample);
// The interval is on the source clock and can contain gaps. Missing samples
// remain absent; they are not replaced by zeros or interpolated observations.
SignalWindow select_signal_window(const SignalWindow&,std::uint64_t first,std::uint64_t count);
SignalWindow read_signal_window(const std::filesystem::path& metadata,std::uint64_t first,
    std::uint64_t count,std::stop_token stop={},std::size_t maximum_payload_bytes=32*1024*1024);

struct SignalWindowBudget {
    std::size_t payload_bytes=8*1024*1024;
    double seconds=1;
    std::size_t maximum_segments=4096;
};
struct SignalWindowStatus {
    std::uint64_t accepted_samples=0,accepted_input_bytes=0,expired_samples=0;
    std::uint64_t known_missing_samples=0,unquantified_discontinuities=0;
    std::size_t retained_payload_bytes=0;
};

// Worker-thread object. It is deliberately not a capture callback: decoding
// and snapshots belong behind the provider's bounded raw-input queue.
// Old preview chunks expire normally; no recording data is overwritten here.
class SignalWindowBuffer {
public:
    explicit SignalWindowBuffer(SignalWindowBudget={});
    void append(const IqBlockView&);
    SignalWindow snapshot(const std::string& source_id={}) const;
    const SignalWindowStatus& status() const noexcept {return status_;}
    void clear();
private:
    SignalWindowBudget budget_;
    std::optional<IqStreamParameters> stream_;
    std::deque<SignalSegment> segments_;
    std::optional<std::uint64_t> next_sample_;
    SignalWindowStatus status_;
};
}
