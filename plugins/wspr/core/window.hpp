#pragma once
#include "search.hpp"
#include <functional>
namespace signalscope::wspr {
struct WindowView {
    std::span<const std::complex<float>> samples;
    std::uint64_t first = 0;
};
class WindowBuffer {
  public:
    static constexpr unsigned window_samples = analysis_rate * 120, interval_samples = analysis_rate * 8;
    WindowBuffer() : ring_(window_samples), ordered_(window_samples) {}
    // Worker-thread only. Views are borrowed until the next publication.
    void append(std::span<const std::complex<float>>, std::uint64_t first, bool discontinuous,
                const std::function<void(WindowView)> &);
    void reset();
    void finish(const std::function<void(WindowView)> &);
    std::size_t retained_samples() const noexcept {
        return filled_;
    }
    std::size_t payload_bytes() const noexcept {
        return (ring_.capacity() + ordered_.capacity()) * sizeof(std::complex<float>);
    }
    std::uint64_t discontinuities() const noexcept {
        return gaps_;
    }

  private:
    std::vector<std::complex<float>> ring_, ordered_;
    unsigned write_ = 0, filled_ = 0, since_ = 0;
    std::uint64_t next_ = 0, gaps_ = 0;
    bool initialized_ = false, published_ = false;
};
} // namespace signalscope::wspr
