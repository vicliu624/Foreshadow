#pragma once
#include "core/search.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/backend/iq/stream.hpp"
namespace signalscope::wspr {
class Downconverter {
  public:
    using Sink = std::function<void(std::span<const std::complex<float>>, std::uint64_t)>;
    Downconverter(IqStreamParameters, double target_hz);
    void feed(std::span<const std::uint8_t>, const Sink &);
    void finish(const Sink &);
    double center_hz() const noexcept {
        return center_;
    }
    double delay_source_samples() const noexcept {
        return delay_;
    }
    std::size_t stages() const noexcept {
        return stages_.size();
    }

  private:
    void route(std::size_t, const ChannelizedBlock &, const Sink &);
    SampleFormat format_;
    double center_, delay_ = 0;
    std::uint64_t direct_first_ = 0;
    std::uint64_t input_pairs_ = 0;
    unsigned input_rate_ = 0;
    bool finished_ = false;
    std::vector<std::unique_ptr<Channelizer>> stages_;
    std::vector<std::complex<float>> decoded_;
};
} // namespace signalscope::wspr
