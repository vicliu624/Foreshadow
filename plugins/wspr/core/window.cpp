#include "window.hpp"
#include <algorithm>
#include <stdexcept>
namespace signalscope::wspr {
void WindowBuffer::reset() {
    write_ = filled_ = since_ = 0;
    initialized_ = published_ = false;
}
void WindowBuffer::finish(const std::function<void(WindowView)> &sink) {
    if (filled_ < frame_samples || (published_ && !since_))
        return;
    const auto begin = (write_ + window_samples - filled_) % window_samples;
    const auto count = std::min(filled_, window_samples - begin);
    std::copy_n(ring_.begin() + begin, count, ordered_.begin());
    std::copy_n(ring_.begin(), filled_ - count, ordered_.begin() + count);
    published_ = true;
    since_ = 0;
    sink({std::span(ordered_).first(filled_), next_ - filled_});
}
void WindowBuffer::append(std::span<const std::complex<float>> input, std::uint64_t first, bool discontinuous,
                          const std::function<void(WindowView)> &sink) {
    if (input.size() > UINT64_MAX - first)
        throw std::overflow_error("WSPR window sample clock overflow");
    if (discontinuous || (initialized_ && first != next_)) {
        ++gaps_;
        reset();
    }
    initialized_ = true;
    for (std::size_t n = 0; n < input.size(); ++n) {
        ring_[write_] = input[n];
        write_ = (write_ + 1) % window_samples;
        filled_ = std::min(window_samples, filled_ + 1);
        if (published_)
            ++since_;
        if (filled_ == window_samples && (!published_ || since_ == interval_samples)) {
            std::copy(ring_.begin() + write_, ring_.end(), ordered_.begin());
            std::copy(ring_.begin(), ring_.begin() + write_, ordered_.begin() + window_samples - write_);
            const auto end = first + n + 1;
            since_ = 0;
            published_ = true;
            sink({ordered_, end - window_samples});
        }
    }
    next_ = first + input.size();
}
} // namespace signalscope::wspr
