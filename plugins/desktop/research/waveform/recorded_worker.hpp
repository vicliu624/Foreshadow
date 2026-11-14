#pragma once
#include "signalscope/backend/dsp/waveform.hpp"
#include "signalscope/backend/dsp/window_research.hpp"
#include "signalscope/backend/dsp/reference_model.hpp"
#include "signalscope/backend/dsp/recovery.hpp"
#include "signalscope/backend/iq/signal_source.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace signalscope::desktop {
struct WaveformSourceIdentity {std::string metadata_sha256,normalized_window_sha256;};
struct RecordedChannelSourceIdentity {std::string metadata_sha256,normalized_source_sha256;};
WaveformSourceIdentity waveform_source_identity(const Json& metadata,const SignalWindow&,std::stop_token stop={});
struct RecordedWaveformCompletion;
struct WaveformModelRequest {
    std::string provider_id;
    unsigned method_version=1;
    Json parameters=Json::object();
    ReferenceFitOptions fit;
    bool processed=false;
    std::optional<SignalSampleRange> range;
    std::optional<SignalSampleClock> expected_clock;
};
struct WaveformRecoveryRequest {
    std::string provider_id;unsigned method_version=1;
    Json parameters=Json::object(),event=Json::object();
    RecoveryBudget budget;bool processed=false;
    std::optional<SignalSampleRange> range;
    std::optional<SignalSampleClock> expected_clock;
};
struct RecordedWaveformRequest {
    std::filesystem::path metadata;
    std::optional<SignalSampleRange> range;
    WaveformOptions options;
    std::size_t source_payload_bytes=8*1024*1024;
    WaveformSourceIdentity expected;
    std::shared_ptr<const SignalWindow> cached;
    std::shared_ptr<const SignalSourceInfo> recording;
    WaveformSourceIdentity cached_identity;
    bool preserve_view=false;
    std::optional<ChannelExtractionOptions> channel;
    std::optional<SignalSampleRange> channel_file_range;
    ChannelSourceBudget channel_source_budget;
    RecordedChannelSourceIdentity expected_channel_source;
    // Combined raw + processed results and temporary FIR coefficient payload.
    // A previous UI result can coexist with one new result during replacement.
    std::size_t result_payload_bytes=128*1024*1024;
    std::optional<WindowResearchOptions> research_options;
    std::optional<SignalSampleRange> research_range;
    bool research_processed=false;
    std::optional<WaveformModelRequest> model;
    std::optional<WaveformRecoveryRequest> recovery;
    // Reuse a successful immutable result for a selection-only calculation.
    // All DSP remains on this one worker, avoiding another pipeline retaining
    // additional generations of the large raw/processed trace results.
    std::shared_ptr<const RecordedWaveformCompletion> completed;
};
struct RecordedWaveformCompletion {
    std::uint64_t generation=0;
    std::shared_ptr<const WaveformAnalysis> analysis;
    std::shared_ptr<const WaveformAnalysis> channel_analysis;
    std::optional<ChannelExtractionOptions> channel;
    std::optional<SignalSampleRange> channel_file_range;
    RecordedChannelSourceIdentity channel_source_identity;
    std::shared_ptr<const WindowResearch> research;
    bool research_processed=false,research_only=false;
    std::shared_ptr<const ReferenceComparison> model;
    std::optional<WaveformModelRequest> model_request;
    std::shared_ptr<const RecoveryResult> recovery;
    std::optional<WaveformRecoveryRequest> recovery_request;
    std::shared_ptr<const SignalSourceInfo> recording;
    SignalSampleRange file_range;
    WaveformSourceIdentity identity;
    WaveformOptions options;
    bool preserve_view=false;
    std::exception_ptr failure;
    std::string error_key;
};

// One running job plus one latest request. Superseding a job cancels it;
// asking for another range never joins a DSP/file thread on the UI thread.
class RecordedWaveformWorker {
public:
    RecordedWaveformWorker();
    ~RecordedWaveformWorker();
    std::uint64_t request(RecordedWaveformRequest);
    void cancel();
    std::shared_ptr<const RecordedWaveformCompletion> takeLatest();
private:
    struct Job {std::uint64_t generation;RecordedWaveformRequest input;std::stop_token stop;};
    void run(std::stop_token);
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Job> pending_;
    std::stop_source active_stop_;
    std::uint64_t generation_=0;
    // Access only through standard shared_ptr atomic operations.
    std::shared_ptr<const RecordedWaveformCompletion> latest_;
    std::jthread thread_;
};
}
