#pragma once
#include "recorded_worker.hpp"
#include "signalscope/backend/dsp/recovery_export.hpp"

namespace signalscope::desktop {
enum class WaveformFilePurpose {analysis,recovery_json,recovery_bytes};
struct WaveformSaveRequest {
    WaveformFilePurpose purpose=WaveformFilePurpose::analysis;
    std::filesystem::path configuration_path;
    Json configuration;
    std::shared_ptr<const SignalWindow> capture;
    WaveformSourceIdentity capture_identity;
    std::vector<std::filesystem::path> protected_paths;
    std::size_t source_payload_bytes=8*1024*1024;
    std::shared_ptr<const RecoveryResult> recovery;
    std::optional<RecoveryLink> selection;
    std::size_t export_maximum_bytes=recoveryExportMaximumBytes;
};
struct WaveformSaveCompletion {
    WaveformFilePurpose purpose=WaveformFilePurpose::analysis;
    std::uint64_t generation=0;
    std::filesystem::path configuration_path,capture_metadata;
    WaveformSourceIdentity identity;
    bool configuration_committed=false,export_committed=false,verified_capture=false,canceled=false;
    std::exception_ptr failure;
    std::string error_key;
};
// One asynchronous save at a time. Cancel never joins a file/DSP thread on
// the UI thread; a committed capture copy is reported even if the subsequent
// configuration write is canceled or fails. Existing sources are protected.
class WaveformAnalysisSaveWorker final {
public:
    WaveformAnalysisSaveWorker();
    ~WaveformAnalysisSaveWorker();
    std::uint64_t request(WaveformSaveRequest);
    void cancel();
    bool busy() const noexcept{return busy_.load();}
    std::shared_ptr<const WaveformSaveCompletion> takeLatest();
private:
    struct Job {std::uint64_t generation;WaveformSaveRequest input;std::stop_token stop;};
    void run(std::stop_token);
    std::mutex mutex_;std::condition_variable condition_;
    std::optional<Job> pending_;std::stop_source active_stop_;
    std::uint64_t generation_=0;
    std::atomic_bool busy_=false;
    // Access only through standard shared_ptr atomic operations.
    std::shared_ptr<const WaveformSaveCompletion> latest_;
    std::jthread thread_;
};
}
