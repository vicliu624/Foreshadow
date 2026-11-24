#include "plugins/wspr/downconverter.hpp"
#include <iostream>
#include <numbers>
int main() {
    using namespace signalscope;
    for (const unsigned rate : {12000u, 2000000u}) {
        const auto seconds = rate == 12000 ? 12u : 4u;
        constexpr double center = 14097000, target = center + 70.3, tone = target + 17.3;
        wspr::Downconverter converter({rate, center, SampleFormat::cf32_le}, target);
        std::vector<std::complex<float>> output;
        std::uint64_t next = 0;
        const auto sink = [&](std::span<const std::complex<float>> samples, std::uint64_t first) {
            if (first != next)
                throw std::runtime_error("WSPR downconverter output clock gap");
            output.insert(output.end(), samples.begin(), samples.end());
            next += samples.size();
        };
        for (unsigned first = 0; first < rate * seconds; first += 1000) {
            const auto count = std::min(1000u, rate * seconds - first);
            Bytes bytes(count * bytes_per_pair(SampleFormat::cf32_le));
            for (unsigned n = 0; n < count; ++n)
                write_iq_pair(
                    bytes.data() + n * bytes_per_pair(SampleFormat::cf32_le), SampleFormat::cf32_le,
                    std::polar(.3f, float(2 * std::numbers::pi * (tone - center) * (first + n) / rate)));
            converter.feed(bytes, sink);
        }
        converter.finish(sink);
        if (output.size() < 375 * seconds ||
            (rate == 12000 ? converter.stages() != 1 : converter.stages() < 2) ||
            converter.delay_source_samples() <= 0)
            throw std::runtime_error("WSPR downconverter length/stage mismatch");
        std::complex<double> correlation{};
        for (std::size_t n = 375; n + 100 < output.size(); ++n)
            correlation += std::conj(std::complex<double>(output[n - 1])) * std::complex<double>(output[n]);
        const auto measured = converter.center_hz() + std::arg(correlation) * 375 / (2 * std::numbers::pi);
        if (std::abs(measured - tone) > .02)
            throw std::runtime_error("WSPR downconverter RF mapping mismatch");
    }
    std::cout << "WSPR downconverter RF, clock, delay and staged planning passed\n";
}
