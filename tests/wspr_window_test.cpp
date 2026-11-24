#include "plugins/wspr/core/window.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    using namespace signalscope::wspr;
    WindowBuffer buffer;
    constexpr std::uint64_t origin = 1ULL << 48;
    std::vector<std::complex<float>> input(analysis_rate * 136);
    for (std::size_t n = 0; n < input.size(); ++n)
        input[n] = {float(n), 0};
    unsigned windows = 0;
    const auto sink = [&](WindowView view) {
        const auto offset = windows * WindowBuffer::interval_samples;
        if (view.first != origin + offset || view.samples.front().real() != offset ||
            view.samples.back().real() != offset + WindowBuffer::window_samples - 1)
            throw std::runtime_error("WSPR window clock/order mismatch");
        ++windows;
    };
    for (std::size_t first = 0; first < input.size(); first += 137) {
        const auto count = std::min<std::size_t>(137, input.size() - first);
        buffer.append(std::span(input).subspan(first, count), origin + first, false, sink);
    }
    if (windows != 3 || buffer.retained_samples() != WindowBuffer::window_samples ||
        buffer.payload_bytes() != 2 * WindowBuffer::window_samples * sizeof(std::complex<float>) ||
        buffer.discontinuities())
        throw std::runtime_error("WSPR window history/budget mismatch");
    buffer.reset();
    unsigned afterGap = 0;
    const auto count = [&](WindowView) { ++afterGap; };
    buffer.append(std::span(input).first(30000), 0, false, count);
    buffer.append(std::span(input).first(20000), 30001, false, count);
    if (afterGap || buffer.discontinuities() != 1)
        throw std::runtime_error("WSPR window bridged missing samples");
    buffer.append(std::span(input).first(25000), 50001, false, count);
    if (afterGap != 1)
        throw std::runtime_error("WSPR window failed after gap recovery");
    std::cout << "WSPR bounded history, arbitrary origin, short chunks and gap checks passed\n";
}
