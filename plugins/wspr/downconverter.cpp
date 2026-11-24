#include "downconverter.hpp"
#include <cmath>
namespace signalscope::wspr {
Downconverter::Downconverter(IqStreamParameters source, double target)
    : format_(source.format), center_(source.center_frequency_hz), input_rate_(source.sample_rate) {
    if (source.sample_rate < analysis_rate || source.sample_rate > 20000000 || !std::isfinite(target) ||
        !std::isfinite(center_) || std::abs(target - center_) + analysis_rate / 2. > source.sample_rate / 2.)
        throw std::invalid_argument("Invalid WSPR downconverter coverage");
    auto rate = source.sample_rate;
    while (rate != analysis_rate) {
        const auto next = rate > 12000 ? std::max(12000u, rate / 32) : analysis_rate;
        auto stage = std::make_unique<Channelizer>(rate, next, format_,
                                                   std::vector<AnalysisChannel>{{target - center_, 300}});
        center_ += stage->channels()[0].offset_hz;
        delay_ += stage->delay_input_samples() * source.sample_rate / rate;
        stages_.push_back(std::move(stage));
        rate = next;
    }
}
void Downconverter::route(std::size_t next, const ChannelizedBlock &block, const Sink &sink) {
    if (next == stages_.size()) {
        const auto limit = std::uint64_t(std::ceil((input_pairs_ + delay_) * analysis_rate / input_rate_));
        if (block.first_sample < limit)
            sink(block.samples.first(std::min<std::size_t>(block.samples.size(), limit - block.first_sample)),
                 block.first_sample);
        return;
    }
    stages_[next]->feed(block.samples,
                        [&](const ChannelizedBlock &result) { route(next + 1, result, sink); });
}
void Downconverter::feed(std::span<const std::uint8_t> bytes, const Sink &sink) {
    const auto stride = bytes_per_pair(format_);
    if (finished_ || bytes.size() % stride || bytes.size() / stride > UINT64_MAX - input_pairs_)
        throw std::invalid_argument("Invalid WSPR converter input");
    input_pairs_ += bytes.size() / stride;
    if (!stages_.empty()) {
        stages_[0]->feed(bytes, [&](const ChannelizedBlock &result) { route(1, result, sink); });
        return;
    }
    decoded_.clear();
    decoded_.reserve(bytes.size() / stride);
    for (std::size_t at = 0; at < bytes.size(); at += stride)
        decoded_.push_back(read_iq_pair(bytes.data() + at, format_));
    sink(decoded_, direct_first_);
    direct_first_ += decoded_.size();
}
void Downconverter::finish(const Sink &sink) {
    if (finished_)
        return;
    finished_ = true;
    for (std::size_t stage = 0; stage < stages_.size(); ++stage)
        stages_[stage]->finish([&](const ChannelizedBlock &block) { route(stage + 1, block, sink); });
}
} // namespace signalscope::wspr
