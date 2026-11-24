#pragma once
#include "core/tracker.hpp"
#include "signalscope/backend/iq/stream.hpp"
#include <memory>
namespace signalscope::adsb {
struct LiveSnapshot {
    std::vector<Aircraft> aircraft;
    std::uint64_t offered_pairs=0,processed_pairs=0,rejected_pairs=0,discontinuities=0;
    std::size_t queued_bytes=0,high_water_bytes=0;
    double source_seconds=0;
    std::string error;
};
class LiveInput {
public:
    static constexpr std::size_t capacity_bytes=8*1024*1024;
    LiveInput();
    ~LiveInput();
    void offer(const IqBlockView&);
    LiveSnapshot snapshot() const;
private:
    struct State;std::unique_ptr<State> state_;
};
}
