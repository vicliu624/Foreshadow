#pragma once
#include "signalscope/backend/dsp/waveform.hpp"

namespace signalscope {
inline constexpr unsigned windowResearchProtocolVersion=1;
enum class ResearchWindow {rectangular,hann,hamming,blackman};
enum class SpectralScaling {power_per_bin,power_density};
struct WindowResearchOptions {
    std::uint32_t fft_size=1024,hop_samples=256;
    ResearchWindow window=ResearchWindow::hann;
    SpectralScaling scaling=SpectralScaling::power_density;
    std::uint32_t frequency_histogram_bins=64,correlation_lags=16;
    std::size_t result_payload_bytes=16*1024*1024,workspace_payload_bytes=4*1024*1024;
    std::uint32_t maximum_frames=4096;
    std::uint64_t maximum_correlation_pairs=20'000'000;
};
struct WindowStatistics {
    std::uint64_t observed_samples=0,missing_clock_positions=0,valid_frequency_samples=0;
    std::complex<double> mean_iq{};
    double mean_power=0,minimum_amplitude=0,maximum_amplitude=0,amplitude_standard_deviation=0;
    double mean_frequency_hz=0,frequency_standard_deviation_hz=0;
};
struct FrequencyHistogram {
    double low_hz=0,high_hz=0;
    std::vector<std::uint64_t> counts;
};
struct CorrelationPoint {
    std::uint32_t lag_samples=0;
    std::uint64_t pairs=0;
    // Sum x[n+lag] conj(x[n]) / pairs; not normalized to a correlation
    // coefficient and not DC removed. No pair crosses a discontinuity.
    std::complex<double> value{};
};
struct WindowResearch {
    std::shared_ptr<const WaveformAnalysis> analysis;
    SignalSampleRange range;
    WindowResearchOptions options;
    WindowStatistics statistics;
    FrequencyHistogram frequency_histogram;
    std::vector<CorrelationPoint> correlation;
    // First index of each complete FFT frame on the analysis clock. Centers
    // are first+(fft_size-1)/2, potentially half a processed sample. Keeping
    // start and support avoids rounding these centers into raw observations.
    std::vector<std::uint64_t> frame_first_samples;
    // Two-sided fftshift order, DC at N/2. Rows are frames, columns bins.
    // Values remain linear; zeros are measured zeros, not a dB display floor.
    std::vector<float> spectral_power;
    std::vector<double> average_spectral_power;
    Json method;
    std::size_t payload_bytes() const;
};
// Explicit selected range, bounded matrix and workspace, no gap padding.
// Statistics use every observed sample; FFT averages use complete frames and
// therefore need not equal the unwindowed, whole-selection average power.
WindowResearch research_signal_window(std::shared_ptr<const WaveformAnalysis>,SignalSampleRange,
    const WindowResearchOptions& options={},std::stop_token stop={});
}
