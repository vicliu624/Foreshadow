#pragma once

#include "signalscope/backend/iq/iq.hpp"
#include <memory>
#include <optional>

namespace signalscope {
inline constexpr std::uint32_t fm_audio_rate=48000;
inline constexpr std::uint32_t fm_minimum_input_rate=225001,fm_maximum_input_rate=100000000;
inline constexpr double fm_channel_bandwidth_hz=200000,fm_channel_guard_hz=110000;

struct FmAudioConfig {
    std::uint32_t input_rate;
    SampleFormat format;
    double offset_hz;
    double deemphasis_us=50;
    double deviation_hz=75000;
};
enum class FmAudioIssue {none,input_rate,frequency,deemphasis,deviation};
FmAudioIssue fmAudioConfigurationIssue(const FmAudioConfig&) noexcept;

using PcmSink=std::function<void(std::span<const std::int16_t>)>;

struct FmSignalMetrics {
    std::uint64_t windows=0;
    std::optional<double> power_dbfs;
    // Constant-envelope M2/M4 estimate after the radio filter. Interference,
    // fading and clipping can bias it; this is not a calibration.
    std::optional<double> snr_db;
};

class FmDemodulator {
public:
    explicit FmDemodulator(const FmAudioConfig&);
    ~FmDemodulator();
    // Chunks must contain whole I/Q pairs and contiguous absolute sample indices.
    void feed(std::span<const std::uint8_t>,std::uint64_t first,const PcmSink&);
    FmSignalMetrics signalMetrics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Json exportFmAudio(const ReplayInfo&,std::uint64_t first,std::uint64_t count,double frequency,
                   double deemphasisUs,double deviationHz,const std::filesystem::path& directory,
                   const std::function<bool()>& cancelled);
}
