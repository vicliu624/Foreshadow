#include "../coherent/common.hpp"

namespace signalscope::recovery {
namespace {
using namespace coherent;
Json describe(const RecoveryContext &c, const Json &pending) {
    const auto order = pending.value("order", Json("qpsk")) == "bpsk" ? 2u : 4u;
    return Json::array({choice("order", "qpsk", {"bpsk", "qpsk"}),
                        number("symbol_rate_hz", c.analysis ? c.analysis->sample_rate() / 8. : 1000, 1, 10'000'000),
                        choice("pulse", "rrc", {"rectangular", "rrc"}), number("rolloff", .35, .05, 1),
                        integer("pulse_span_symbols", 8, 4, 16), text("training", training(order)),
                        integer("payload_symbols", 256, 8, 4096), text("data_reference", ""),
                        integer("search_samples", 1024, 1, 8192), number("clock_search_ppm", 1000, 0, 5000),
                        integer("channel_taps", 3, 1, 9), integer("equalizer_taps", 5, 1, 15),
                        number("minimum_rms", .001, 1e-8, 10), number("minimum_correlation", .65, .3, .99),
                        number("maximum_holdout_evm", .35, .01, .8)});
}
double rrc(double x, double b) {
    if (std::abs(x) < 1e-10)
        return 1 - b + 4 * b / pi;
    if (std::abs(std::abs(4 * b * x) - 1) < 1e-8)
        return b / std::sqrt(2.) * ((1 + 2 / pi) * std::sin(pi / (4 * b)) + (1 - 2 / pi) * std::cos(pi / (4 * b)));
    return (std::sin(pi * (1 - b) * x) + 4 * b * x * std::cos(pi * (1 + b) * x)) / (pi * x * (1 - 16 * b * b * x * x));
}
struct Candidate {
    double score = 0, start = 0, period = 0;
    C gain{};
};
RecoveryData recover(const RecoveryContext &context, const Json &supplied, std::stop_token stop) {
    Work w{context, stop};
    RecoveryData d;
    d.kind = RecoveryKind::digital;
    if (!parameters(d, supplied, describe(context, supplied)))
        return d;
    const auto &p = d.parameters;
    const unsigned order = p.at("order") == "bpsk" ? 2 : 4, payload = p.at("payload_symbols"),
                   ct = p.at("channel_taps"), et = p.at("equalizer_taps");
    const double fs = context.analysis->sample_rate(), period = fs / p.at("symbol_rate_hz").get<double>();
    std::vector<unsigned> train, reference;
    try {
        train = symbols(p.at("training"), order, 256);
        reference = symbols(p.at("data_reference"), order);
    } catch (...) {
        issue(d, "training", "issue.symbols");
        return d;
    }
    if (period < 2 || period > 64 || train.size() < 48 || train.size() > 256 ||
        (!reference.empty() && reference.size() != payload) || !(ct & 1) || !(et & 1) || ct > train.size() / 4 ||
        et > train.size() / 4) {
        issue(d, "training", "issue.configuration");
        return d;
    }
    const auto total = train.size() + payload;
    const auto resultEstimate =
        (total * 4 + payload * 5 + ct + et) * (sizeof(RecoveryValue) + 3 * sizeof(RecoveryLink) + 64) + 32768;
    if (resultEstimate > context.budget.result_payload_bytes || total * 4 + payload * 5 > context.budget.maximum_values)
        throw std::length_error("PSK requested result exceeds budget");
    auto x = input(context, w);
    std::size_t fn = 2;
    while (fn < x.size() && fn < 8192)
        fn <<= 1;
    if (fn > x.size())
        fn >>= 1;
    if (fn < 128) {
        noLock(d, "sync.short", w);
        return d;
    }
    std::vector<C> power(fn);
    for (std::size_t n = 0; n < fn; ++n) {
        C z = x[n];
        const auto mag = std::abs(z);
        if (mag > p.at("minimum_rms").get<double>()) {
            z /= mag;
            power[n] = order == 2 ? z * z : z * z * z * z;
        }
    }
    fft(power, false, w);
    const auto peak = static_cast<std::size_t>(
        std::max_element(power.begin(), power.end(), [](C a, C b) { return std::norm(a) < std::norm(b); }) -
        power.begin());
    const auto signedPeak = peak > fn / 2 ? static_cast<double>(peak) - fn : double(peak);
    double cfo = signedPeak * fs / (fn * order);
    const double maxCfo = fs / (2 * order);
    for (std::size_t n = 0; n < x.size(); ++n)
        x[n] *= std::polar(1., -2 * pi * cfo * n / fs);
    w.tick(x.size() * 12);
    const bool shaped = p.at("pulse") == "rrc";
    const auto half =
        static_cast<int>(std::ceil((shaped ? p.at("pulse_span_symbols").get<double>() / 2 : .5) * period));
    std::vector<double> taps(static_cast<std::size_t>(2 * half + 1));
    double norm = 0;
    for (int n = -half; n <= half; ++n) {
        const auto t = shaped                                    ? rrc(n / period, p.at("rolloff"))
                       : std::abs(double(n)) < period / 2        ? 1.
                       : std::abs(double(n)) - period / 2 < 1e-8 ? .5
                                                                 : 0.;
        taps[n + half] = t;
        norm += t;
    }
    for (auto &t : taps)
        t /= norm;
    std::vector<C> filtered(x.size());
    for (std::size_t n = half; n + half < x.size(); ++n) {
        w.tick(taps.size() * 8);
        for (int k = -half; k <= half; ++k)
            filtered[n] += x[n + k] * taps[k + half];
    }
    power.clear();
    power.shrink_to_fit();
    const double ppm = p.at("clock_search_ppm");
    const auto score = [&](double start, double step) {
        Candidate c;
        c.start = start;
        c.period = step;
        if (start < half || start + (train.size() - 1) * step + half + 2 >= x.size())
            return c;
        C sum = 0;
        double energy = 0;
        for (std::size_t n = 0; n < train.size(); ++n) {
            const auto z = interpolate(filtered, start + n * step);
            sum += z * std::conj(point(train[n], order));
            energy += std::norm(z);
        }
        w.tick(train.size() * 16);
        if (energy > train.size() * std::pow(p.at("minimum_rms").get<double>(), 2)) {
            c.score = std::norm(sum) / (train.size() * energy);
            c.gain = sum / double(train.size());
        }
        return c;
    };
    Candidate best, runner;
    const auto search = std::min<std::size_t>(p.at("search_samples").get<std::size_t>(), x.size());
    for (std::size_t at = half; at < search; ++at) {
        auto c = score(double(at), period);
        if (c.score > best.score) {
            if (std::abs(c.start - best.start) > period * 1.5)
                runner = best;
            best = c;
        } else if (std::abs(c.start - best.start) > period * 1.5 && c.score > runner.score)
            runner = c;
    }
    auto coarse = best;
    for (int clock = -4; clock <= 4; ++clock)
        for (int fraction = -8; fraction <= 8; ++fraction) {
            const auto c = score(coarse.start + fraction / 8., period * (1 + ppm * clock / 4e6));
            if (c.score > best.score)
                best = c;
        }
    d.method = {{"classification", "not-performed"},
                {"synchronization",
                 {{"locked", false},
                  {"coarse_cfo_hz", cfo},
                  {"cfo_unambiguous_range_hz", maxCfo},
                  {"correlation", best.score},
                  {"alternate_correlation", runner.score}}},
                {"mapping", "natural-phase-index-msb-first"},
                {"data_reference_used_for_estimation", false}};
    if (best.score < p.at("minimum_correlation").get<double>() ||
        (runner.score > best.score * .97 && runner.score > .8)) {
        noLock(d, "sync.ambiguous", w);
        return d;
    }
    // Training-only phase slope refines the M-th-power acquisition. Unwrap
    // adjacent residual phases; payload/reference never participates.
    double previous = 0, sumT = 0, sumP = 0, sumTT = 0, sumTP = 0;
    for (std::size_t n = 2; n + 2 < train.size(); ++n) {
        double phase =
            std::arg(interpolate(filtered, best.start + n * best.period) * std::conj(point(train[n], order)));
        if (n > 2) {
            while (phase - previous > pi)
                phase -= 2 * pi;
            while (phase - previous < -pi)
                phase += 2 * pi;
        }
        previous = phase;
        sumT += n;
        sumP += phase;
        sumTT += double(n) * n;
        sumTP += n * phase;
    }
    const double np = train.size() - 4, slope = (sumTP - sumT * sumP / np) / (sumTT - sumT * sumT / np);
    const double fine = slope * fs / (2 * pi * best.period);
    cfo += fine;
    for (std::size_t n = 0; n < x.size(); ++n)
        x[n] *= std::polar(1., -2 * pi * fine * n / fs);
    std::fill(filtered.begin(), filtered.end(), C{});
    for (std::size_t n = half; n + half < x.size(); ++n) {
        w.tick(taps.size() * 8);
        for (int k = -half; k <= half; ++k)
            filtered[n] += x[n + k] * taps[k + half];
    }
    coarse = best;
    best.score = 0;
    for (int clock = -4; clock <= 4; ++clock)
        for (int fraction = -8; fraction <= 8; ++fraction) {
            const auto c = score(coarse.start + fraction / 16., coarse.period * (1 + ppm * clock / 16e6));
            if (c.score > best.score)
                best = c;
        }
    const auto radius = std::max(ct, et) / 2;
    const auto fitEnd = train.size() * 3 / 4;
    if (best.start + (total + radius) * best.period + half + 2 >= x.size()) {
        noLock(d, "sync.short", w);
        return d;
    }
    std::vector<C> received(total + radius);
    for (std::size_t n = 0; n < received.size(); ++n)
        received[n] = interpolate(filtered, best.start + n * best.period);
    std::vector<std::vector<C>> rows;
    std::vector<C> target;
    for (std::size_t n = radius; n + radius < fitEnd; ++n) {
        std::vector<C> row(ct);
        for (unsigned k = 0; k < ct; ++k)
            row[k] = point(train[n + k - ct / 2], order);
        rows.push_back(std::move(row));
        target.push_back(received[n]);
    }
    const auto channel = leastSquares(rows, target, 1e-5, w);
    rows.clear();
    target.clear();
    for (std::size_t n = radius; n + radius < fitEnd; ++n) {
        std::vector<C> row(et);
        for (unsigned k = 0; k < et; ++k)
            row[k] = received[n + k - et / 2];
        rows.push_back(std::move(row));
        target.push_back(point(train[n], order));
    }
    const auto equalizer = leastSquares(rows, target, 1e-5, w);
    const auto equalized = [&](double n) {
        C z = 0;
        for (unsigned k = 0; k < et; ++k)
            z += interpolate(filtered, best.start + (n + int(k) - int(et / 2)) * best.period) * equalizer[k];
        w.tick(et * 20);
        return z;
    };
    double holdError = 0;
    unsigned holdCount = 0;
    for (std::size_t n = fitEnd + radius; n + radius < train.size(); ++n) {
        holdError += std::norm(equalized(double(n)) - point(train[n], order));
        ++holdCount;
    }
    const auto holdout = holdCount ? std::sqrt(holdError / holdCount) : 1.;
    d.method["synchronization"] = {{"locked", holdout <= p.at("maximum_holdout_evm").get<double>()},
                                   {"start_samples", best.start},
                                   {"samples_per_symbol", best.period},
                                   {"clock_offset_ppm", (best.period / period - 1) * 1e6},
                                   {"cfo_hz", cfo},
                                   {"correlation", best.score},
                                   {"training_holdout_evm", holdout},
                                   {"holdout_symbols", holdCount}};
    if (holdout > p.at("maximum_holdout_evm").get<double>()) {
        noLock(d, "sync.holdout", w);
        return d;
    }
    d.stages = {{"acquisition",
                 "stage.sync",
                 {},
                 {{"training_symbols", train.size()}, {"coarse_method", "M-th-power FFT then training correlation"}},
                 d.method.at("synchronization"),
                 Json::object()},
                {"channel",
                 "stage.channel",
                 {0},
                 {{"estimator", "training-only regularized LS"}, {"ridge", 1e-5}, {"taps", ct}},
                 Json::object(),
                 Json::object()},
                {"equalization",
                 "stage.equalizer",
                 {0, 1},
                 {{"estimator", "training-only regularized LS FFE"}, {"taps", et}, {"ridge", 1e-5}},
                 {{"holdout_rms", holdout}},
                 Json::object()},
                {"decisions",
                 "stage.decisions",
                 {2},
                 {{"mapping", "natural-phase-index-msb-first"}},
                 Json::object(),
                 Json::object()}};
    d.sequences = {plane("acquisition", "sequence.acquisition", 0, "domain.input"),
                   plane("channel", "sequence.channel", 1, "domain.channel"),
                   plane("equalizer", "sequence.equalizer", 2, "domain.equalizer"),
                   plane("observed", "sequence.observed", 0, "domain.symbols"),
                   plane("equalized", "sequence.equalized", 2, "domain.symbols"),
                   plane("reference", "sequence.reference", 2, "domain.symbols", true)};
    const auto acquisition = support(
        context, 0, std::max<double>(fn, std::min<double>(x.size() - 1, search + train.size() * best.period + half)));
    d.sequences[0].values.push_back(complexValue(best.gain, acquisition, 0, 0));
    for (unsigned k = 0; k < channel.size(); ++k) {
        auto v = complexValue(channel[k], {}, 0, int(k) - int(ct / 2));
        v.inputs = {{0, 0, 1}};
        d.sequences[1].values.push_back(v);
    }
    for (unsigned k = 0; k < equalizer.size(); ++k) {
        auto v = complexValue(equalizer[k], {}, 0, int(k) - int(et / 2));
        v.inputs = {{0, 0, 1}};
        d.sequences[2].values.push_back(v);
    }
    Evm evm;
    double phase = 0, frequency = 0, timing = 0, omega = 0;
    const auto limit = period * .2;
    for (unsigned n = 0; n < payload; ++n) {
        const auto s = double(train.size() + n) + timing / best.period;
        const auto center = best.start + s * best.period;
        const auto range = support(context, best.start + train.size() * best.period - (et / 2) * best.period - half - 1,
                                   center + (et / 2) * best.period + half + 2);
        const auto z0 = interpolate(filtered, center);
        auto observed = complexValue(z0, range, 0, int(n));
        observed.inputs = {{0, 0, 1}}; // Include local support through its own leaf below.
        observed.inputs.clear();
        d.sequences[3].values.push_back(std::move(observed));
        C z = equalized(s) * std::polar(1., -phase);
        const auto decision = point(slice(z, order), order);
        const double error = std::clamp(std::arg(z * std::conj(decision)), -.5, .5);
        const bool known = std::abs(z) > .15 && std::abs(z) < 3 && std::abs(error) < pi / order * .9;
        auto v = complexValue(z, {}, 0, int(n), known);
        v.inputs = {{0, 0, 1}, {2, 0, static_cast<unsigned>(equalizer.size())}, {3, n, 1}};
        v.metric = std::abs(error);
        d.sequences[4].values.push_back(v);
        const auto r = reference.empty() ? decision : point(reference[n], order);
        auto ref = complexValue(r, {}, 0, int(n), known);
        ref.inputs = {{4, n, 1}};
        d.sequences[5].values.push_back(ref);
        evm.add(z, r, known);
        frequency = std::clamp(frequency + .0004 * error, -.05, .05);
        phase += frequency + .04 * error;
        const auto early = equalized(s - .15), late = equalized(s + .15);
        const auto timingError = std::clamp(std::real((late - early) * std::conj(z * std::polar(1., phase))), -.5, .5);
        omega = std::clamp(omega + .00001 * timingError, -period * .002, period * .002);
        timing = std::clamp(timing + omega + .003 * timingError, -limit, limit);
    }
    d.method["evm"] = evm.json(reference.empty() ? "decision-directed" : "known-data");
    d.method["tracking"] = {{"phase_rad", phase},
                            {"frequency_rad_per_symbol", frequency},
                            {"timing_samples", timing},
                            {"clock_step_samples", omega}};
    d.stages[2].features["evm"] = d.method["evm"];
    pack(d, 4, order, w);
    const auto workspace = (x.capacity() + filtered.capacity() + received.capacity() + fn) * sizeof(C) +
                           taps.capacity() * sizeof(double) + 32768;
    finish(d, w, workspace);
    return d;
}
} // namespace
RecoveryProvider psk() { return {"trained-psk-receiver", "recovery-psk", "label", "about", 2, 1, describe, recover}; }
} // namespace signalscope::recovery
