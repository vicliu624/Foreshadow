#pragma once
#include "signalscope/backend/iq/signal_window.hpp"
#include <memory>

namespace signalscope {
struct SignalCaptureBudget {
    std::size_t queue_payload_bytes=2*1024*1024;
    std::size_t maximum_block_bytes=256*1024;
    std::size_t maximum_queued_blocks=128;
    SignalWindowBudget retention;
};
struct SignalCaptureStatus {
    std::uint64_t generation=0;
    bool active=false;
    std::size_t queued_payload_bytes=0,peak_queued_payload_bytes=0,queued_blocks=0,in_flight_payload_bytes=0;
    std::uint64_t accepted_samples=0,processed_samples=0,rejected_samples=0,canceled_samples=0,processing_rejected_samples=0;
    std::uint64_t malformed_blocks=0,oversized_blocks=0,contention_blocks=0,queue_full_blocks=0,configuration_rejected_blocks=0,inactive_blocks=0;
    // Lifetime counters; stale-source input is not charged to the new source.
    std::uint64_t stale_generation_samples_total=0,stale_generation_blocks_total=0;
    SignalWindowStatus window;
    std::string error;
};
struct SignalCaptureSnapshot {
    std::uint64_t request=0,generation=0;
    std::shared_ptr<const SignalWindow> window;
    SignalCaptureStatus status;
    std::string error;
};
// One producer and one worker; the producer copies borrowed payload under a
// try-lock, performs no normalization/DSP/file I/O and never waits on the worker.
// A snapshot is an asynchronous FIFO barrier over samples already accepted at
// request time. Later input continues; gaps and source generations stay explicit.
class SignalCapture final {
public:
    explicit SignalCapture(SignalCaptureBudget={});
    ~SignalCapture();
    SignalCapture(const SignalCapture&)=delete;
    SignalCapture& operator=(const SignalCapture&)=delete;
    std::uint64_t start(IqStreamParameters,std::string source_id,Json context=Json::object());
    void stop() noexcept;
    // Owner/control thread only. Permanently stops and joins the worker before
    // shared producer callbacks can become its final owner; start then rejects.
    void close() noexcept;
    bool offer(const IqBlockView&,std::uint64_t expected_generation=0) noexcept;
    std::uint64_t requestSnapshot();
    std::shared_ptr<const SignalCaptureSnapshot> takeSnapshot();
    SignalCaptureStatus status() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
