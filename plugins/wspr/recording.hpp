#pragma once
#include "live_input.hpp"
#include "signalscope/backend/iq/signal_source.hpp"
namespace signalscope::wspr {
struct RecordingResult {
    std::filesystem::path metadata;
    SignalSampleRange stored_range;
    LiveSnapshot result;
};
RecordingResult decode_recording(const std::filesystem::path &, double target_hz,
                                 const SearchOptions &options = {},
                                 std::optional<SignalSampleRange> range = {}, std::stop_token stop = {});
} // namespace signalscope::wspr
