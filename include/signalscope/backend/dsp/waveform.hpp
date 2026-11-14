#pragma once

#include "signalscope/backend/dsp/signal_processing.hpp"
#include <memory>

namespace signalscope {
inline constexpr unsigned analysisTraceProtocolVersion=4;
struct AnalysisTraceSpan {
    std::uint64_t first_sample=0;
    std::size_t first_value=0,count=0;
    bool discontinuous_before=false;
    bool operator==(const AnalysisTraceSpan&) const=default;
};
struct AnalysisTrace {
    std::string id,label_key,unit;
    // NaN means invalid or unavailable, and must never be interpolated by a
    // consumer. Spans map values to exact indices on this analysis clock;
    // source_position() retains a processed point's fractional raw position.
    std::vector<double> values;
    // v4: optional packed subset on the parent analysis clock. Each mapped
    // point must be an observed parent point; gaps and unknown discontinuities
    // remain boundaries. Null keeps the v1-v3 complete-parent mapping.
    std::shared_ptr<const std::vector<AnalysisTraceSpan>> mapping{};
};
struct WaveformOptions {
    double minimum_amplitude=1e-8;
    double relative_amplitude_floor=.05;
    unsigned frequency_smoothing_samples=9;
    std::size_t output_payload_bytes=128*1024*1024;
};
std::size_t waveform_sample_capacity(const WaveformOptions&) noexcept;
struct WaveformAnalysis {
    // Original RAM observations when available. A file-backed processed input
    // instead retains its recording descriptor; source is then null. Spans
    // and cursors use the explicit analysis clock, never implied raw indices.
    std::shared_ptr<const SignalWindow> source;
    std::shared_ptr<const ProcessedSignal> processed;
    std::vector<AnalysisTraceSpan> spans;
    std::vector<AnalysisTrace> traces;
    Json method;
    const AnalysisTrace& trace(const std::string& id) const;
    std::optional<std::size_t> value_index(std::uint64_t source_sample) const;
    std::size_t payload_bytes() const;
    std::uint64_t sample_count() const;
    std::uint32_t sample_rate() const;
    std::uint64_t first_sample() const;
    std::uint64_t end_sample() const;
    std::uint64_t time_origin() const;
    double time_offset_seconds() const;
    SourceSamplePosition source_position(std::uint64_t sample) const;
};
const std::vector<AnalysisTraceSpan>& analysis_trace_spans(const WaveformAnalysis&,const AnalysisTrace&);
std::optional<std::size_t> analysis_trace_value_index(const WaveformAnalysis&,const AnalysisTrace&,std::uint64_t);
void validate_analysis_trace(const WaveformAnalysis&,const AnalysisTrace&);
std::size_t analysis_traces_payload_bytes(const std::vector<AnalysisTrace>&);
// Both recorded and live callers use this exact numerical kernel. No Qt,
// receiver implementation or modulation-specific model is involved.
WaveformAnalysis analyze_waveform(std::shared_ptr<const SignalWindow>,const WaveformOptions& options={},std::stop_token stop={});
WaveformAnalysis analyze_waveform(std::shared_ptr<const ProcessedSignal>,const WaveformOptions& options={},std::stop_token stop={});
}
