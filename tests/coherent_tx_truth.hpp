#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <algorithm>
#include <complex>
#include <numbers>
#include <random>
namespace coherent_truth {
using C = std::complex<double>;
inline constexpr double pi = std::numbers::pi;
struct Fixture {
    std::vector<C> samples;
    std::vector<unsigned> payload;
    signalscope::Json parameters;
};
inline std::vector<unsigned> randomSymbols(unsigned count, unsigned order, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<unsigned> v(count);
    for (auto &a : v)
        a = rng() % order;
    return v;
}
inline std::string list(const std::vector<unsigned> &v) {
    std::string s;
    for (auto n : v) {
        if (!s.empty())
            s += ' ';
        s += std::to_string(n);
    }
    return s;
}
inline C constellation(unsigned n, unsigned order) { return std::exp(C(0, 2 * pi * n / order)); }
// Spectral quadrature of the square root raised-cosine response. This TX
// oracle does not use the receiver's closed-form time-domain RRC formula.
inline double pulse(double t, double beta = .35) {
    constexpr unsigned steps = 192;
    const auto width = (1 + beta) / 2;
    double sum = 0;
    for (unsigned j = 0; j < steps; ++j) {
        const double f = (j + .5) * width / steps;
        const auto response =
            f <= (1 - beta) / 2 ? 1. : std::sqrt(.5 * (1 + std::cos(pi * (f - (1 - beta) / 2) / beta)));
        sum += response * std::cos(2 * pi * f * t);
    }
    return 2 * sum * width / steps;
}
inline Fixture psk(unsigned order = 4, bool shaped = true, unsigned payloadCount = 128, double clockPpm = 250,
                   double carrier = 79.3) {
    Fixture f;
    const auto train = randomSymbols(64, order, 137);
    f.payload = randomSymbols(payloadCount, order, 793);
    auto all = train;
    all.insert(all.end(), f.payload.begin(), f.payload.end());
    const double origin = 53.35, sps = 8 * (1 + clockPpm / 1e6);
    const auto wave = [&](double t) {
        const auto center = (t - origin) / sps;
        const auto nearest = static_cast<int>(std::floor(center + .5));
        if (!shaped)
            return nearest >= 0 && nearest < int(all.size()) ? constellation(all[nearest], order) : C{};
        C sum = 0;
        for (int symbol = std::max(0, nearest - 4); symbol < std::min(int(all.size()), nearest + 5); ++symbol) {
            const auto delta = center - symbol;
            if (std::abs(delta) <= 4)
                sum += constellation(all[symbol], order) * pulse(delta);
        }
        return sum;
    };
    f.samples.resize(static_cast<std::size_t>(origin + (all.size() + 8) * sps));
    std::mt19937 rng(73);
    std::normal_distribution<double> noise(0, .002);
    for (unsigned n = 0; n < f.samples.size(); ++n)
        f.samples[n] = (.7 * std::polar(1., .6) * wave(n) + C(.16, -.07) * wave(n - 2.2)) *
                           std::polar(1., 2 * pi * carrier * n / 8000) +
                       C(noise(rng), noise(rng));
    f.parameters = {{"order", order == 2 ? "bpsk" : "qpsk"},
                    {"pulse", shaped ? "rrc" : "rectangular"},
                    {"training", list(train)},
                    {"payload_symbols", payloadCount},
                    {"data_reference", list(f.payload)},
                    {"symbol_rate_hz", 1000},
                    {"search_samples", 128}};
    return f;
}
inline std::vector<C> idft(const std::vector<C> &bins) {
    std::vector<C> y(bins.size());
    for (unsigned t = 0; t < bins.size(); ++t)
        for (unsigned k = 0; k < bins.size(); ++k)
            y[t] += bins[k] * std::exp(C(0, 2 * pi * k * t / bins.size())) / std::sqrt(double(bins.size()));
    return y;
}
inline Fixture ofdm(unsigned count = 8, double clockPpm = 150, double cfo = 1.18, bool notch = false) {
    Fixture f;
    constexpr unsigned n = 64, cp = 16;
    std::vector<int> active, pilots{-21, -7, 7, 21};
    for (int k = -26; k <= 26; ++k)
        if (k)
            active.push_back(k);
    const auto train = randomSymbols(active.size(), 2, 247);
    f.payload = randomSymbols((active.size() - pilots.size()) * count, 4, 818);
    std::vector<C> base(40);
    std::vector<std::vector<C>> frequencies;
    unsigned data = 0;
    for (unsigned s = 0; s < count + 2; ++s) {
        std::vector<C> frequency(n);
        for (unsigned a = 0; a < active.size(); ++a) {
            const auto k = active[a];
            const unsigned value = s < 2                                                        ? train[a]
                                   : std::find(pilots.begin(), pilots.end(), k) != pilots.end() ? 0
                                                                                                : f.payload[data++];
            frequency[(k + int(n)) % int(n)] = constellation(value, s < 2 ? 2 : 4);
        }
        frequencies.push_back(frequency);
        const auto y = idft(frequency);
        base.insert(base.end(), y.end() - cp, y.end());
        base.insert(base.end(), y.begin(), y.end());
    }
    base.resize(base.size() + 80);
    const auto sample = [&](double p) {
        if (p < 40 || p >= 40 + (count + 2) * (n + cp))
            return C{};
        const double position = p - 40;
        const auto symbol = static_cast<unsigned>(std::floor(position / (n + cp)));
        const auto time = position - symbol * (n + cp) - cp;
        C value = 0;
        for (unsigned k = 0; k < n; ++k) {
            const int signedBin = k > n / 2 ? int(k) - int(n) : int(k);
            value += frequencies[symbol][k] * std::exp(C(0, 2 * pi * signedBin * time / n)) / std::sqrt(double(n));
        }
        return value;
    };
    f.samples.resize(base.size());
    std::mt19937 rng(27);
    std::normal_distribution<double> noise(0, notch ? .00002 : .0007);
    for (unsigned t = 0; t < f.samples.size(); ++t) {
        const double position = t / (1 + clockPpm / 1e6);
        const auto z = notch ? sample(position) - sample(position - 4)
                             : C(.7, .2) * sample(position) + C(.15, -.09) * sample(position - 2);
        f.samples[t] = z * std::polar(1., 2 * pi * cfo * t / n) + C(noise(rng), noise(rng));
    }
    f.parameters = {{"training", list(train)},
                    {"payload_symbols", count},
                    {"data_reference", list(f.payload)},
                    {"search_samples", 100}};
    return f;
}
} // namespace coherent_truth
