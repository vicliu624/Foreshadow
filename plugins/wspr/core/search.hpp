#pragma once
#include "channel.hpp"
#include <complex>
#include <stop_token>
#include <vector>
namespace signalscope::wspr {
inline constexpr unsigned analysis_rate = 375, samples_per_symbol = 256, frame_samples = 162 * 256;
struct SearchOptions {
    double minimum_hz = -110, maximum_hz = 110;
    unsigned maximum_drift_hz = 4, maximum_candidates = 32, cycles_per_bit = 10000;
    std::size_t workspace_bytes = 8 * 1024 * 1024;
};
struct Observation {
    std::uint64_t first = 0;
    double base_hz = 0, drift_hz = 0;
    ChannelFrame decoded;
};
// A continuous, normalized complex 375 S/s observation, at most 120 seconds.
// Input discontinuities must be split/reset by the owning stream adapter.
std::vector<Observation> search(std::span<const std::complex<float>>, const SearchOptions &options = {},
                                std::stop_token stop = {});
} // namespace signalscope::wspr
