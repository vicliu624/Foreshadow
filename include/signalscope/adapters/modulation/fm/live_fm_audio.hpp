#pragma once

#include "signalscope/adapters/modulation/fm_audio.hpp"

namespace signalscope {
inline constexpr std::size_t fm_live_maximum_block_bytes=1024*1024;
struct LiveFmAudioStatus {
    std::uint64_t dropped_blocks=0;
    std::uint64_t discontinuities=0;
    bool failed=false;
    std::uint64_t input_pairs=0,processed_pairs=0,dropped_pairs=0;
    std::uint64_t queue_bytes=0,queue_capacity_bytes=0,queue_high_water_bytes=0;
    std::uint64_t pcm_frames=0,pcm_discarded_frames=0;
    FmSignalMetrics signal;
    std::string error_detail;
};

// One capture producer offers borrowed blocks. A separate worker owns the
// demodulator. The control/consumer thread owns start, stop and read.
class LiveFmAudio {
public:
    LiveFmAudio();
    ~LiveFmAudio();
    void start(const FmAudioConfig&,std::size_t maximumBlockBytes);
    void stop();
    void offer(std::span<const std::uint8_t>,std::uint64_t first) noexcept;
    std::size_t read(std::span<std::int16_t> destination);
    LiveFmAudioStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
