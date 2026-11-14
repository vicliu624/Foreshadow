#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <memory>

namespace signalscope {
struct AnalysisChannel {
    double offset_hz=0;
    double search_width_hz=0;
};
struct ChannelizedBlock {
    std::span<const std::complex<float>> samples;
    std::size_t channel=0;
    std::uint64_t first_sample=0; // continuous output-rate index after the latest reset
};
using ChannelizedSink=std::function<void(const ChannelizedBlock&)>;

// Shared forward FFT; overlap-save anti-alias filtering and frequency-domain
// decimation per channel, followed by fractional conversion when required.
// Input indices remain external: output indices map to input first + n*Fs/Fout
// minus delay_input_samples(). Gaps must reset all downstream detector state.
class Channelizer {
public:
    Channelizer(std::uint32_t input_rate,std::uint32_t output_rate,SampleFormat,
                std::vector<AnalysisChannel> channels);
    ~Channelizer();
    Channelizer(const Channelizer&)=delete;
    Channelizer& operator=(const Channelizer&)=delete;
    void feed(std::span<const std::uint8_t>,const ChannelizedSink&);
    // Already decoded, normalized samples; the same filtering and sample clock.
    void feed(std::span<const std::complex<float>>,const ChannelizedSink&);
    void finish(const ChannelizedSink&);
    void reset();
    std::span<const AnalysisChannel> channels() const;
    double delay_input_samples() const;
    std::uint32_t output_rate() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
std::vector<AnalysisChannel> planAnalysisChannels(std::uint32_t input_rate,std::uint32_t output_rate,
                                               double search_width_hz,double signal_bandwidth_hz,double edge_bandwidth_hz=0);
}
