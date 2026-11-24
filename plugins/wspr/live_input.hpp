#pragma once
#include "core/search.hpp"
#include "signalscope/backend/iq/stream.hpp"
#include <memory>
namespace signalscope::wspr {
struct Spot {
    Observation observation;
    double frequency_hz = 0;
    std::uint64_t source_sample = 0;
    unsigned source_rate = 0;
    std::uint64_t epoch = 0;
};
struct LiveSnapshot {
    std::vector<Spot> spots;
    std::uint64_t offered_pairs = 0, processed_pairs = 0, input_rejected_pairs = 0, discontinuities = 0;
    std::uint64_t completed_windows = 0, rejected_windows = 0;
    std::uint64_t decoded_spots_total = 0, expired_spots = 0;
    std::size_t queued_bytes = 0, input_in_flight_bytes = 0, window_queue_bytes = 0,
                window_in_flight_bytes = 0;
    std::size_t retained_samples = 0;
    // Queue charge: owned vector capacity plus block objects, not process RSS.
    std::size_t queued_memory_bytes = 0;
    double queued_input_seconds = 0, input_in_flight_seconds = 0;
    bool finished = false;
    std::string error;
};
class LiveInput {
  public:
    static constexpr std::size_t input_budget_bytes = 16 * 1024 * 1024, maximum_block_bytes = 1024 * 1024,
                                 window_budget_bytes = 2 * 120 * 375 * sizeof(std::complex<float>);
    static constexpr double input_budget_seconds = 240;
    explicit LiveInput(double target_hz, SearchOptions options = {});
    ~LiveInput();
    void offer(const IqBlockView &);
    void finish();
    // Owning/control thread: join DSP before dropping callback-held references.
    void stop();
    LiveSnapshot snapshot() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace signalscope::wspr
