#include "search.hpp"
#include "sync.hpp"
#include <algorithm>
#include <cmath>
#include <kiss_fft.h>
#include <memory>
#include <numbers>
#include <stdexcept>
namespace signalscope::wspr {
namespace {
constexpr unsigned fft_size = 512, hop = 64;
struct Candidate {
    double score;
    unsigned row;
    int bin, drift;
};
} // namespace
std::vector<Observation> search(std::span<const std::complex<float>> input, const SearchOptions &options,
                                std::stop_token stop) {
    if (!std::isfinite(options.minimum_hz) || !std::isfinite(options.maximum_hz) ||
        options.minimum_hz < -150 || options.maximum_hz > 150 || options.minimum_hz >= options.maximum_hz ||
        options.maximum_drift_hz > 8 || !options.maximum_candidates || options.maximum_candidates > 64 ||
        !options.cycles_per_bit || options.cycles_per_bit > 100000)
        throw std::invalid_argument("Invalid WSPR search configuration");
    if (input.size() > analysis_rate * 120)
        throw std::length_error("WSPR observation exceeds 120 seconds");
    if (input.size() < frame_samples)
        return {};
    for (const auto value : input)
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
            throw std::invalid_argument("Invalid WSPR input sample");
    const auto rows = (input.size() - samples_per_symbol) / hop + 1;
    using Spectrum = std::array<float, fft_size>;
    if (rows * sizeof(Spectrum) + 2 * fft_size * sizeof(kiss_fft_cpx) + 65536 > options.workspace_bytes)
        throw std::length_error("WSPR spectral workspace budget exceeded");
    std::vector<Spectrum> spectrum(rows);
    std::unique_ptr<kiss_fft_state, decltype(&kiss_fft_free)> plan(
        kiss_fft_alloc(fft_size, 0, nullptr, nullptr), kiss_fft_free);
    if (!plan)
        throw std::bad_alloc();
    std::array<kiss_fft_cpx, fft_size> in{}, out{};
    for (std::size_t row = 0; row < rows; ++row) {
        if (stop.stop_requested())
            throw std::runtime_error("WSPR search canceled");
        for (unsigned n = 0; n < samples_per_symbol; ++n) {
            const auto value = input[row * hop + n];
            in[n] = {value.real(), value.imag()};
        }
        kiss_fft(plan.get(), in.data(), out.data());
        for (unsigned bin = 0; bin < fft_size; ++bin) {
            const auto value = out[bin].r * out[bin].r + out[bin].i * out[bin].i;
            if (!std::isfinite(value))
                throw std::invalid_argument("WSPR spectral power overflow");
            spectrum[row][bin] = value;
        }
    }
    const auto power = [&](unsigned row, int bin) {
        return double(spectrum[row][(bin % int(fft_size) + fft_size) % fft_size]);
    };
    std::vector<Candidate> candidates;
    const auto firstBin = int(std::ceil(options.minimum_hz * fft_size / analysis_rate)),
               lastBin = int(std::floor(options.maximum_hz * fft_size / analysis_rate));
    const auto lastRow = (input.size() - frame_samples) / hop;
    for (unsigned row = 0; row <= lastRow; ++row) {
        if (stop.stop_requested())
            throw std::runtime_error("WSPR search canceled");
        for (int bin = firstBin; bin <= lastBin; ++bin)
            for (int drift = -int(options.maximum_drift_hz); drift <= int(options.maximum_drift_hz);
                 ++drift) {
                double sync = 0, total = 0;
                for (unsigned symbol = 0; symbol < 162; ++symbol) {
                    const auto at = row + symbol * 4;
                    const int b =
                        bin + int(std::lround(drift * (symbol + .5) / 162 * fft_size / analysis_rate));
                    const auto s = sync_bits[symbol];
                    const auto a = power(at, b + 2 * s) + power(at, b + 2 * s + 4),
                               other = power(at, b + 2 * (1 - s)) + power(at, b + 2 * (3 - s));
                    sync += a - other;
                    total += a + other;
                }
                const auto score = sync / std::max(total, 1e-30);
                if (score < .12)
                    continue;
                auto near = std::find_if(candidates.begin(), candidates.end(), [&](const auto &c) {
                    return std::abs(c.bin - bin) < 4 && std::abs(int(c.row) - int(row)) < 5;
                });
                if (near != candidates.end()) {
                    if (score > near->score)
                        *near = {score, row, bin, drift};
                    continue;
                }
                candidates.push_back({score, row, bin, drift});
                std::sort(candidates.begin(), candidates.end(),
                          [](const auto &a, const auto &b) { return a.score > b.score; });
                if (candidates.size() > options.maximum_candidates)
                    candidates.pop_back();
            }
    }
    std::vector<Observation> result;
    for (const auto &candidate : candidates) {
        if (stop.stop_requested())
            throw std::runtime_error("WSPR search canceled");
        TonePowers powers{};
        for (unsigned symbol = 0; symbol < 162; ++symbol) {
            const int bin =
                candidate.bin +
                int(std::lround(candidate.drift * (symbol + .5) / 162 * fft_size / analysis_rate));
            for (unsigned tone = 0; tone < 4; ++tone)
                powers[symbol][tone] = power(candidate.row + symbol * 4, bin + 2 * tone);
        }
        if (auto decoded = decode_channel(powers, options.cycles_per_bit)) {
            const auto first = std::uint64_t(candidate.row) * hop;
            const double hz = double(candidate.bin) * analysis_rate / fft_size;
            if (std::none_of(result.begin(), result.end(), [&](const auto &old) {
                    return old.decoded.message.payload == decoded->message.payload &&
                           std::abs(old.base_hz - hz) < 3 &&
                           std::abs(double(old.first) - double(first)) < analysis_rate;
                }))
                result.push_back({first, hz, double(candidate.drift), std::move(*decoded)});
        }
    }
    return result;
}
} // namespace signalscope::wspr
