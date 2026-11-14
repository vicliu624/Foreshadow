#include "coherent_tx_truth.hpp"
#include "signalscope/backend/dsp/recovery.hpp"
#include "signalscope/backend/dsp/recovery_export.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
using namespace signalscope;
namespace {
using C = std::complex<double>;
constexpr double pi = std::numbers::pi;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejected(F f, const char *message) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
std::string list(const std::vector<unsigned> &v) {
    std::string s;
    for (auto n : v) {
        if (!s.empty())
            s += ' ';
        s += std::to_string(n);
    }
    return s;
}
std::vector<unsigned> randomSymbols(unsigned count, unsigned order, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<unsigned> v(count);
    for (auto &a : v)
        a = rng() % order;
    return v;
}
C constellation(unsigned n, unsigned order) {
    const auto a = 2 * pi * n / order;
    return {std::cos(a), std::sin(a)};
}
std::shared_ptr<WaveformAnalysis> analysis(const std::vector<C> &samples) {
    auto window = std::make_shared<SignalWindow>();
    window->stream = {8000, 433012000, SampleFormat::cf32_le};
    window->source_id = "independent-coherent-TX";
    window->recording_origin = (1ULL << 54) + 123;
    SignalSegment segment;
    segment.first_sample = window->recording_origin;
    for (auto z : samples)
        segment.samples.emplace_back(float(z.real()), float(z.imag()));
    window->segments.push_back(std::move(segment));
    return std::make_shared<WaveformAnalysis>(analyze_waveform(window));
}
RecoveryResult receive(const std::vector<C> &samples, const char *id, const Json &p, RecoveryBudget budget = {}) {
    auto a = analysis(samples);
    return recover_signal(a, {a->first_sample(), a->sample_count()}, recovery_provider(id), p, budget);
}
const RecoverySequence &sequence(const RecoveryResult &r, const char *id) {
    for (const auto &s : r.sequences)
        if (s.id == id)
            return s;
    throw std::runtime_error("Missing coherent sequence");
}
void truth(const RecoveryResult &r, const std::vector<unsigned> &payload, unsigned order, double maximum) {
    std::cerr << r.provider_id << ' ' << r.method.dump() << '\n';
    require(r.method.at("synchronization").at("locked"), "Receiver did not acquire independent transmitted burst");
    const auto &z = sequence(r, "equalized").values;
    require(z.size() == payload.size(), "Receiver changed payload extent");
    double error = 0, peak = 0;
    unsigned count = 0;
    for (unsigned n = 0; n < payload.size(); ++n) {
        if (!z[n].known)
            continue;
        const auto e = std::norm(z[n].complex_value - constellation(payload[n], order));
        error += e;
        peak = std::max(peak, e);
        ++count;
    }
    require(count >= payload.size() * .95, "Receiver erased ordinary held-out data");
    const double rms = 100 * std::sqrt(error / count);
    require(rms < maximum, "Held-out EVM exceeds independent impairment envelope");
    require(std::abs(r.method.at("evm").at("rms_percent").get<double>() - rms) < 1e-8,
            "Reported EVM differs from independent point-by-point oracle");
    require(std::abs(r.method.at("evm").at("peak_percent").get<double>() - 100 * std::sqrt(peak)) < 1e-8,
            "Peak EVM uses an inconsistent normalization");
    const auto &decisions = sequence(r, "decisions").values;
    unsigned wrong = 0;
    for (unsigned n = 0; n < payload.size(); ++n)
        wrong += !decisions[n].known || decisions[n].value != payload[n];
    require(wrong == 0, "Independent payload decisions contain bit errors");
    require(r.frames.size() == 1 && r.frames[0].state == RecoveryFrameState::candidate,
            "Good EVM incorrectly validated an unchecked application frame");
    const auto ranges =
        recovery_input_ranges(r, static_cast<unsigned>(&sequence(r, "bytes") - r.sequences.data()), 0, 1);
    require(!ranges.empty() && ranges.front().first == r.range.first,
            "Byte provenance omitted synchronization/training evidence");
    std::string json;
    export_recovery_json(r, {{"independent", true}}, [&](auto s) { json.append(s); });
    auto evidence = Json::parse(json);
    require(evidence.at("version") == 2 && evidence.at("sequences")[1].at("kind") == "complex",
            "Complex evidence was silently down-converted to v1");
}
void psk() {
    for (auto order : {2u, 4u}) {
        const auto train = randomSymbols(64, order, 137), payload = randomSymbols(128, order, 793);
        auto all = train;
        all.insert(all.end(), payload.begin(), payload.end());
        const double origin = 53.35, sps = 8.002, cfo = 79.3;
        const auto wave = [&](double t) {
            const auto at = static_cast<int>(std::floor((t - origin) / sps + .5));
            return at >= 0 && at < int(all.size()) ? constellation(all[at], order) : C{};
        };
        std::vector<C> samples(static_cast<std::size_t>(origin + (all.size() + 8) * sps));
        std::mt19937 rng(73);
        std::normal_distribution<double> noise(0, .002);
        for (unsigned n = 0; n < samples.size(); ++n)
            samples[n] = (.7 * std::polar(1., .6) * wave(n) + C(.16, -.07) * wave(n - 2.2)) *
                             std::polar(1., 2 * pi * cfo * n / 8000) +
                         C(noise(rng), noise(rng));
        Json p = {{"order", order == 2 ? "bpsk" : "qpsk"},
                  {"pulse", "rectangular"},
                  {"training", list(train)},
                  {"payload_symbols", payload.size()},
                  {"data_reference", list(payload)},
                  {"symbol_rate_hz", 1000},
                  {"search_samples", 128}};
        const auto result = receive(samples, "trained-psk-receiver", p);
        truth(result, payload, order, 20);
        require(std::abs(result.method.at("synchronization").at("cfo_hz").get<double>() - cfo) < 2,
                "PSK carrier estimate misses independent truth");
        auto noRef = p;
        noRef["data_reference"] = "";
        const auto dd = receive(samples, "trained-psk-receiver", noRef);
        const auto &x = sequence(result, "equalized").values;
        const auto &y = sequence(dd, "equalized").values;
        for (unsigned n = 0; n < x.size(); ++n)
            require(x[n].complex_value == y[n].complex_value, "Known payload leaked into receiver estimation");
        auto changed = p;
        auto wrong = payload;
        for (auto &v : wrong)
            v = (v + 1) % order;
        changed["data_reference"] = list(wrong);
        const auto mismatch = receive(samples, "trained-psk-receiver", changed);
        require(mismatch.method.at("evm").at("rms_percent").get<double>() > 100,
                "Wrong known reference was fitted away");
        std::fill(samples.begin(), samples.end(), C{});
        const auto silent = receive(samples, "trained-psk-receiver", p);
        require(!silent.method.at("synchronization").at("locked") && !silent.method.at("evm").at("available"),
                "Silent PSK input acquired an EVM claim");
    }
}
// Independent explicit inverse DFT, not the receiver's radix-2 FFT.
std::vector<C> idft(const std::vector<C> &bins) {
    std::vector<C> y(bins.size());
    for (unsigned t = 0; t < bins.size(); ++t)
        for (unsigned k = 0; k < bins.size(); ++k)
            y[t] += bins[k] * std::exp(C(0, 2 * pi * k * t / bins.size())) / std::sqrt(double(bins.size()));
    return y;
}
void ofdm() {
    constexpr unsigned n = 64, cp = 16;
    std::vector<int> active, pilots{-21, -7, 7, 21};
    for (int k = -26; k <= 26; ++k)
        if (k)
            active.push_back(k);
    const auto train = randomSymbols(active.size(), 2, 247),
               payload = randomSymbols((active.size() - pilots.size()) * 8, 4, 818);
    std::vector<C> base(40);
    unsigned data = 0;
    for (unsigned s = 0; s < 10; ++s) {
        std::vector<C> frequency(n);
        for (unsigned a = 0; a < active.size(); ++a) {
            const auto k = active[a];
            unsigned v = s < 2                                                        ? train[a]
                         : std::find(pilots.begin(), pilots.end(), k) != pilots.end() ? 0
                                                                                      : payload[data++];
            frequency[(k + int(n)) % int(n)] = constellation(v, s < 2 ? 2 : 4);
        }
        const auto y = idft(frequency);
        base.insert(base.end(), y.end() - cp, y.end());
        base.insert(base.end(), y.begin(), y.end());
    }
    base.resize(base.size() + 80);
    const auto sample = [&](double p) {
        if (p < 0 || p + 1 >= base.size())
            return C{};
        const auto k = static_cast<unsigned>(p);
        return base[k] + (base[k + 1] - base[k]) * (p - k);
    };
    std::vector<C> samples(base.size());
    std::mt19937 rng(27);
    std::normal_distribution<double> noise(0, .0007);
    for (unsigned t = 0; t < samples.size(); ++t) {
        const double position = t / 1.00015;
        const auto z = C(.7, .2) * sample(position) + C(.15, -.09) * sample(position - 2);
        samples[t] = z * std::polar(1., 2 * pi * 1.18 * t / n) + C(noise(rng), noise(rng));
    }
    Json p = {{"training", list(train)}, {"data_reference", list(payload)}, {"search_samples", 100}};
    const auto r = receive(samples, "trained-ofdm-receiver", p);
    truth(r, payload, 4, 25);
    require(std::abs(r.method.at("synchronization").at("cfo_bins").get<double>() - 1.18) < .01,
            "OFDM integer/fractional CFO misses independent truth");
    auto dd = p;
    dd["data_reference"] = "";
    const auto other = receive(samples, "trained-ofdm-receiver", dd);
    for (unsigned a = 0; a < payload.size(); ++a)
        require(sequence(r, "equalized").values[a].complex_value ==
                    sequence(other, "equalized").values[a].complex_value,
                "Known OFDM data leaked into CE/equalization");
    std::fill(samples.begin(), samples.end(), C{});
    const auto empty = receive(samples, "trained-ofdm-receiver", p);
    require(!empty.method.at("synchronization").at("locked") && !empty.method.at("evm").at("available"),
            "Silent OFDM input acquired an EVM claim");
}
void shaped() {
    for (auto order : {2u, 4u}) {
        const auto f = coherent_truth::psk(order);
        truth(receive(f.samples, "trained-psk-receiver", f.parameters), f.payload, order, 15);
    }
}
void clockTracking() {
    const auto f = coherent_truth::psk(4, true, 1024, 900, -163.1);
    truth(receive(f.samples, "trained-psk-receiver", f.parameters), f.payload, 4, 18);
    const auto o = coherent_truth::ofdm(32, 400, -1.27);
    truth(receive(o.samples, "trained-ofdm-receiver", o.parameters), o.payload, 4, 28);
    const auto notch = coherent_truth::ofdm(8, 0, 0, true);
    const auto faded = receive(notch.samples, "trained-ofdm-receiver", notch.parameters);
    require(faded.method.at("synchronization").at("locked"), "Notched OFDM lost an otherwise observable preamble");
    unsigned erased = 0;
    for (const auto &v : sequence(faded, "equalized").values)
        if (!v.known) {
            ++erased;
            require(v.coordinate == -16 || v.coordinate == 16, "Ordinary carrier erased by notch handling");
        }
    std::cerr << "notch erasures " << erased << " " << faded.method.dump() << "\n";
    for (const auto &v : sequence(faded, "channel").values)
        if (v.coordinate == -16 || v.coordinate == 16)
            std::cerr << v.coordinate << " H " << v.complex_value << "\n";
    require(erased == 16 && faded.method.at("evm").at("excluded") == 16,
            "Deep-fade carriers were promoted to valid EVM data");
    auto wrong = o.parameters;
    wrong["training"] = list(randomSymbols(52, 2, 88));
    const auto bad = receive(o.samples, "trained-ofdm-receiver", wrong);
    require(!bad.method.at("synchronization").at("locked"), "Wrong OFDM training acquired qualified EVM");
}
void limits() {
    const auto f = coherent_truth::psk(4, false, 32);
    for (unsigned kind = 0; kind < 8; ++kind) {
        RecoveryBudget b;
        if (kind == 0)
            b.workspace_payload_bytes = 1;
        if (kind == 1)
            b.result_payload_bytes = 1;
        if (kind == 2)
            b.maximum_values = 1;
        if (kind == 3)
            b.maximum_operations = 1;
        if (kind == 4)
            b.maximum_links = 1;
        if (kind == 5)
            b.maximum_input_positions = 1;
        if (kind == 6)
            b.metadata_bytes = 1;
        if (kind == 7)
            b.maximum_frames = 0;
        rejected([&] { receive(f.samples, "trained-psk-receiver", f.parameters, b); },
                 "Coherent receiver ignored a resource budget");
    }
    auto a = analysis(f.samples);
    auto broken = std::make_shared<WaveformAnalysis>(*a);
    broken->spans.front().discontinuous_before = true; // First span can begin a fresh burst; interior breaks cannot.
    auto source = std::make_shared<SignalWindow>(*a->source);
    auto tail = source->segments.front();
    tail.first_sample += 100;
    tail.samples.erase(tail.samples.begin(), tail.samples.begin() + 100);
    tail.discontinuous_before = true;
    source->segments.front().samples.resize(100);
    source->segments.push_back(tail);
    const auto gap = std::make_shared<WaveformAnalysis>(analyze_waveform(source));
    rejected(
        [&] {
            recover_signal(gap, {gap->first_sample(), gap->sample_count()}, recovery_provider("trained-psk-receiver"),
                           f.parameters);
        },
        "Coherent receiver bridged an interior discontinuity");
    std::stop_source stop;
    stop.request_stop();
    rejected(
        [&] {
            recover_signal(a, {a->first_sample(), a->sample_count()}, recovery_provider("trained-psk-receiver"),
                           f.parameters, {}, Json::object(), stop.get_token());
        },
        "Canceled coherent receiver published output");
    auto wrong = f.parameters;
    wrong["training"] = list(randomSymbols(64, 4, 831));
    const auto bad = receive(f.samples, "trained-psk-receiver", wrong);
    require(!bad.method.at("synchronization").at("locked"), "Wrong PSK preamble acquired a qualified EVM");
}
void channelTruth() {
    const auto fixture = coherent_truth::ofdm(8, 0, .33);
    const auto r = receive(fixture.samples, "trained-ofdm-receiver", fixture.parameters);
    auto unordered = fixture.parameters;
    std::string reversed;
    for (int bin = 26; bin >= -26; --bin)
        if (bin) {
            if (!reversed.empty())
                reversed += ' ';
            reversed += std::to_string(bin);
        }
    unordered["active_bins"] = reversed;
    rejected([&] { receive(fixture.samples, "trained-ofdm-receiver", unordered); },
             "Bin sorting silently rebound the supplied training values");
    const auto anchor = r.method.at("synchronization").at("fft_anchor_samples").get<double>();
    C cross = 0;
    double expectedPower = 0;
    std::vector<C> expected;
    for (const auto &v : sequence(r, "channel").values) {
        const double k = v.coordinate;
        const auto h = (C(.7, .2) + C(.15, -.09) * std::exp(C(0, -2 * pi * k * 2 / 64))) *
                       std::exp(C(0, 2 * pi * k * (anchor - 56) / 64));
        expected.push_back(h);
        cross += v.complex_value * std::conj(h);
        expectedPower += std::norm(h);
    }
    const auto gain = cross / expectedPower;
    double error = 0, power = 0;
    for (unsigned k = 0; k < expected.size(); ++k) {
        error += std::norm(sequence(r, "channel").values[k].complex_value - gain * expected[k]);
        power += std::norm(gain * expected[k]);
    }
    require(std::sqrt(error / power) < .01,
            "Estimated OFDM frequency response disagrees with independent physical two-path channel");
    std::mt19937 random(98);
    std::normal_distribution<double> noise(0, .2);
    auto samples = fixture.samples;
    for (auto &z : samples)
        z = {noise(random), noise(random)};
    const auto none = receive(samples, "trained-ofdm-receiver", fixture.parameters);
    require(!none.method.at("synchronization").at("locked") && !none.method.at("evm").at("available"),
            "Noise-only OFDM input acquired a qualified measurement");
}
void independent() {
    std::vector<C> samples(256, C(.3, .2));
    auto a = analysis(samples);
    RecoveryProvider p{
        "independent.vector-measurement",
        "external.vector",
        "external.label",
        "external.about",
        2,
        1,
        [](const auto &, const auto &) { return Json::array(); },
        [](const RecoveryContext &c, const auto &, auto) {
            RecoveryData d;
            d.kind = RecoveryKind::measurement;
            d.stages = {{"future", "future.stage", {}, Json::object(), Json::object(), Json::object()}};
            RecoverySequence s{"external-complex", "external.sequence", 0, RecoveryValueKind::complex, {}};
            s.domain_key = "external.domain";
            RecoveryValue v;
            v.known = true;
            v.complex_value = {.3, .2};
            v.coordinate = -9;
            v.group = 7;
            v.support = {c.range.first + 5, 3};
            s.values.push_back(v);
            d.sequences.push_back(s);
            return d;
        }};
    register_recovery(p);
    auto r = recover_signal(a, {a->first_sample(), a->sample_count()}, p, Json::object());
    require(r.sequences[0].values[0].coordinate == -9 && recovery_input_ranges(r, 0, 0, 1)[0].count == 3,
            "Independent v2 measurement lost its generic capability");
    r.protocol_version = 1;
    rejected([&] { validate_recovery(r); }, "v1 accepted an undeclared complex capability");
    r.protocol_version = 2;
    r.sequences[0].values[0].complex_value = {NAN, 0};
    rejected([&] { validate_recovery(r); }, "Nonfinite constellation point entered the contract");
}
} // namespace
int main() {
    try {
        psk();
        shaped();
        ofdm();
        clockTracking();
        limits();
        channelTruth();
        independent();
        std::cout << "Independent coherent waveform, held-out EVM, reference isolation and protocol extension passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
