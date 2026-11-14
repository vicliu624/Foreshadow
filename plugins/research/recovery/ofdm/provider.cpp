#include "../coherent/common.hpp"

namespace signalscope::recovery {
namespace {
using namespace coherent;
std::vector<int> bins(const std::string &text, unsigned n) {
    std::vector<int> out;
    std::string_view s = text;
    while (!s.empty()) {
        while (!s.empty() && (s.front() == ' ' || s.front() == ','))
            s.remove_prefix(1);
        if (s.empty())
            break;
        int k = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), k);
        if (r.ec != std::errc{} || r.ptr == s.data() || k <= -int(n) / 2 || k >= int(n) / 2 ||
            std::find(out.begin(), out.end(), k) != out.end() || out.size() >= n)
            throw std::invalid_argument("Invalid OFDM bin allocation");
        out.push_back(k);
        s.remove_prefix(static_cast<std::size_t>(r.ptr - s.data()));
        if (!s.empty() && s.front() != ' ' && s.front() != ',')
            throw std::invalid_argument("Invalid bin separator");
    }
    // Training and pilot values are positional. Reordering only the bin list
    // silently changes those bindings, so require the declared canonical order.
    if (!std::is_sorted(out.begin(), out.end()))
        throw std::invalid_argument("OFDM bins must be ascending");
    return out;
}
std::string activeDefault(unsigned n) {
    std::string s;
    for (int k = -int(n) * 13 / 32; k <= int(n) * 13 / 32; ++k)
        if (k) {
            if (!s.empty())
                s += ' ';
            s += std::to_string(k);
        }
    return s;
}
Json describe(const RecoveryContext &, const Json &pending) {
    const auto v = pending.value("fft_size", Json(64));
    const auto n = v.is_number_unsigned() || v.is_number_integer() ? std::clamp(v.get<unsigned>(), 16u, 1024u) : 64u;
    const auto active = activeDefault(n);
    return Json::array(
        {integer("fft_size", 64, 16, 1024), integer("cyclic_prefix", 16, 2, 256), text("active_bins", active),
         text("training", training(2, bins(active, n).size())), text("pilot_bins", "-21 -7 7 21"),
         text("pilot_symbols", "0 0 0 0"), choice("order", "qpsk", {"bpsk", "qpsk"}),
         integer("payload_symbols", 8, 1, 128), text("data_reference", ""), integer("search_samples", 512, 1, 8192),
         integer("integer_cfo_bins", 2, 0, 8), integer("channel_span_samples", 8, 1, 128),
         number("minimum_rms", .001, 1e-8, 10), number("minimum_sync_score", .65, .2, .99),
         number("deep_fade_ratio", .03, .0001, .5), choice("equalizer", "regularized", {"zf", "regularized"})});
}
struct Candidate {
    double score = 0, cp = 0, cfo = 0, concentration = 0, repeat = 1;
    unsigned start = 0;
    std::vector<C> channel;
    double noise = 0;
};
RecoveryData recover(const RecoveryContext &context, const Json &supplied, std::stop_token stop) {
    Work w{context, stop};
    RecoveryData d;
    d.kind = RecoveryKind::digital;
    if (!parameters(d, supplied, describe(context, supplied)))
        return d;
    const auto &p = d.parameters;
    const unsigned n = p.at("fft_size"), cp = p.at("cyclic_prefix"), count = p.at("payload_symbols"),
                   span = p.at("channel_span_samples"), order = p.at("order") == "bpsk" ? 2 : 4,
                   maxInteger = p.at("integer_cfo_bins");
    std::vector<int> active, pilots;
    std::vector<unsigned> train, pilotValues, reference;
    try {
        active = bins(p.at("active_bins"), n);
        pilots = bins(p.at("pilot_bins"), n);
        train = symbols(p.at("training"), 2, 1024);
        pilotValues = symbols(p.at("pilot_symbols"), order, 1024);
        reference = symbols(p.at("data_reference"), order, 4096);
    } catch (...) {
        issue(d, "training", "issue.symbols");
        return d;
    }
    if ((n & (n - 1)) || cp >= n || span > cp || active.size() < 8 || train.size() != active.size() ||
        pilots.size() < 2 || pilots.size() != pilotValues.size() ||
        !std::includes(active.begin(), active.end(), pilots.begin(), pilots.end()) ||
        (!reference.empty() && reference.size() != count * (active.size() - pilots.size()))) {
        issue(d, "training", "issue.configuration");
        return d;
    }
    const auto dataCount = count * (active.size() - pilots.size());
    const auto estimate =
        (dataCount * 8 + active.size() * 3 + count + 2) * (sizeof(RecoveryValue) + 4 * sizeof(RecoveryLink) + 64) +
        65536;
    if (estimate > context.budget.result_payload_bytes || dataCount * 8 > context.budget.maximum_values)
        throw std::length_error("OFDM requested result exceeds budget");
    auto x = input(context, w);
    const unsigned length = n + cp;
    const auto idx = [&](int k) { return static_cast<unsigned>((k + int(n)) % int(n)); };
    const auto transform = [&](double start, double cfo) {
        std::vector<C> out(n);
        for (unsigned j = 0; j < n; ++j) {
            const double at = start + j;
            const auto center = static_cast<int>(std::floor(at));
            const double fraction = at - center;
            if (fraction < 1e-10) {
                out[j] = x.at(center) * std::polar(1., -2 * pi * cfo * center / n);
                continue;
            }
            double normalization = 0;
            C value = 0;
            for (int k = -11; k <= 12; ++k) {
                const int position = center + k;
                if (position < 0 || position >= int(x.size()))
                    throw std::out_of_range("OFDM fractional sampling needs interpolation guards");
                const double delta = at - position;
                const double sinc = std::abs(delta) < 1e-12 ? 1. : std::sin(pi * delta) / (pi * delta);
                const double window = .42 + .5 * std::cos(pi * delta / 12) + .08 * std::cos(2 * pi * delta / 12);
                const double weight = sinc * window;
                normalization += weight;
                value += x[position] * std::polar(1., -2 * pi * cfo * position / n) * weight;
            }
            out[j] = value / normalization;
            w.tick(24 * 32);
        }
        fft(out, false, w);
        return out;
    };
    const auto end = std::min<std::size_t>(p.at("search_samples").get<std::size_t>(),
                                           x.size() > 2 * length + 2 ? x.size() - 2 * length - 2 : 0);
    std::vector<std::pair<double, unsigned>> scores;
    scores.reserve(end);
    for (unsigned start = 0; start < end; ++start) {
        C corr = 0;
        double a = 0, b = 0;
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned j = 0; j < cp; ++j) {
                const auto z = x[start + t * length + j], r = x[start + t * length + n + j];
                corr += std::conj(z) * r;
                a += std::norm(z);
                b += std::norm(r);
            }
        w.tick(cp * 2 * 16);
        const auto score = a * b > 1e-20 ? std::norm(corr) / (a * b) : 0;
        if (a / (2 * cp) >= std::pow(p.at("minimum_rms").get<double>(), 2))
            scores.emplace_back(score, start);
    }
    std::sort(scores.begin(), scores.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    Candidate best, alternate;
    unsigned evaluated = 0;
    std::vector<unsigned> tried;
    for (const auto &[cpScore, start] : scores) {
        if (cpScore < .25 || evaluated >= 48)
            break; // Bound FFT candidates; retain separated CP plateaus.
        if (std::any_of(tried.begin(), tried.end(),
                        [&](unsigned t) { return std::abs(int(t) - int(start)) < std::max(1, int(cp / 4)); }))
            continue;
        tried.push_back(start);
        ++evaluated;
        C corr = 0;
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned j = 0; j < cp; ++j)
                corr += std::conj(x[start + t * length + j]) * x[start + t * length + n + j];
        const double fractional = std::arg(corr) / (2 * pi);
        for (int integer = -int(maxInteger); integer <= int(maxInteger); ++integer) {
            const double cfo = fractional + integer;
            auto first = transform(start + cp, cfo), second = transform(start + length + cp, cfo);
            std::vector<C> h(n);
            double signal = 0, noise = 0;
            for (unsigned a = 0; a < active.size(); ++a) {
                const auto k = idx(active[a]);
                h[k] = (first[k] + second[k]) * .5 / std::polar(1., pi * train[a]);
                noise += std::norm(first[k] - second[k]) * .5;
                signal += (std::norm(first[k]) + std::norm(second[k])) * .5;
            }
            noise /= active.size();
            if (signal < 1e-15)
                continue;
            auto impulse = h;
            fft(impulse, true, w);
            double total = 0, window = 0, compact = 0;
            for (auto z : impulse)
                total += std::norm(z);
            for (unsigned j = 0; j < span; ++j)
                window += std::norm(impulse[j]);
            compact = window;
            for (unsigned j = 1; j < n; ++j) {
                window -= std::norm(impulse[j - 1]);
                window += std::norm(impulse[(j + span - 1) % n]);
                compact = std::max(compact, window);
            }
            const double concentration = total > 0 ? compact / total : 0, repeat = noise * active.size() / signal,
                         score = cpScore * concentration * std::max(0., 1 - repeat);
            if (score > best.score) {
                if (std::abs(int(start) - int(best.start)) > int(length))
                    alternate = best;
                best = {score, cpScore, cfo, concentration, repeat, start, std::move(h), noise};
            } else if (std::abs(int(start) - int(best.start)) > int(length) && score > alternate.score)
                alternate = {score, cpScore, cfo, concentration, repeat, start, {}, noise};
        }
    }
    d.method = {{"classification", "not-performed"},
                {"data_reference_used_for_estimation", false},
                {"synchronization",
                 {{"locked", false},
                  {"score", best.score},
                  {"cp_score", best.cp},
                  {"channel_concentration", best.concentration},
                  {"repeat_noise_ratio", best.repeat},
                  {"cfo_bins", best.cfo},
                  {"fft_anchor_samples", best.start + cp},
                  {"alternate_score", alternate.score}}},
                {"fft_normalization", "unitary"},
                {"training_symbols", 2},
                {"timing_semantics", "valid FFT window; CP propagation delay is not uniquely identified"}};
    if (best.score < p.at("minimum_sync_score").get<double>() || alternate.score > best.score * .97) {
        noLock(d, "sync.ambiguous", w);
        return d;
    }
    if (best.start + (count + 2) * length + 2 >= x.size()) {
        noLock(d, "sync.short", w);
        return d;
    }
    // A CP-correlation peak can lie after the true CP boundary in multipath.
    // Place the FFT inside the CP rather than taking a late window that leaks
    // the next symbol. Its phase ramp is absorbed by the channel estimate.
    const auto advance = std::min(span - 1, cp / 2);
    best.start -= std::min(best.start, advance);
    d.method["synchronization"]["fft_anchor_samples"] = best.start + cp;
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        const auto a = transform(best.start + cp, best.cfo), b = transform(best.start + length + cp, best.cfo);
        C rotation = 0;
        for (auto k : active)
            rotation += std::conj(a[idx(k)]) * b[idx(k)];
        best.cfo += std::arg(rotation) * n / (2 * pi * length);
    }
    const auto t0 = transform(best.start + cp, best.cfo), t1 = transform(best.start + length + cp, best.cfo);
    best.noise = 0;
    for (unsigned a = 0; a < active.size(); ++a) {
        const auto k = idx(active[a]);
        best.channel[k] = (t0[k] + t1[k]) * .5 / std::polar(1., pi * train[a]);
        best.noise += std::norm(t0[k] - t1[k]) * .5 / active.size();
    }
    d.method["synchronization"]["cfo_bins"] = best.cfo;
    d.method["synchronization"]["locked"] = true;
    d.method["synchronization"]["cfo_hz"] = best.cfo * context.analysis->sample_rate() / n;
    d.stages = {{"acquisition",
                 "stage.sync",
                 {},
                 {{"method", "CP correlation, repeated training and integer CFO search"}},
                 d.method["synchronization"],
                 Json::object()},
                {"channel",
                 "stage.channel",
                 {0},
                 {{"method", "two known repeated training symbols; per-active-bin LS"}},
                 {{"noise_variance", best.noise}},
                 Json::object()},
                {"equalizer",
                 "stage.equalizer",
                 {0, 1},
                 {{"method", p.at("equalizer")}, {"regularization_source", "training difference noise estimate"}},
                 Json::object(),
                 Json::object()},
                {"decisions",
                 "stage.decisions",
                 {2},
                 {{"mapping", "natural-phase-index-msb-first"}},
                 Json::object(),
                 Json::object()}};
    d.sequences = {plane("acquisition", "sequence.acquisition", 0, "domain.input"),
                   plane("channel", "sequence.channel", 1, "domain.frequency"),
                   plane("impulse", "sequence.impulse", 1, "domain.channel"),
                   plane("observed", "sequence.observed", 0, "domain.carriers"),
                   plane("pilots", "sequence.pilots", 2, "domain.carriers"),
                   plane("equalized", "sequence.equalized", 2, "domain.carriers"),
                   plane("reference", "sequence.reference", 2, "domain.carriers", true)};
    d.sequences[0].values.push_back(complexValue(C(best.score, 0), support(context, 0, end + 2 * length), 0, 0));
    double meanH = 0;
    for (unsigned a = 0; a < active.size(); ++a) {
        auto v = complexValue(best.channel[idx(active[a])], {}, 0, active[a]);
        v.inputs = {{0, 0, 1}};
        d.sequences[1].values.push_back(v);
        meanH += std::norm(v.complex_value);
    }
    meanH /= active.size();
    auto impulse = best.channel;
    fft(impulse, true, w);
    for (unsigned j = 0; j < n; ++j) {
        auto v = complexValue(impulse[j], {}, 0, j);
        v.inputs = {{1, 0, static_cast<unsigned>(active.size())}};
        d.sequences[2].values.push_back(v);
    }
    const double fade = meanH * std::pow(p.at("deep_fade_ratio").get<double>(), 2),
                 regularizer = p.at("equalizer") == "zf" ? 0 : best.noise;
    Evm evm;
    double sampling = 0, drift = 0, lastSlope = 0;
    bool haveSlope = false;
    unsigned dataIndex = 0;
    for (unsigned s = 0; s < count; ++s) {
        const double anchor = best.start + (s + 2) * length + cp + sampling;
        auto spectrum = transform(anchor, best.cfo);
        const auto window = support(context, anchor - 12, anchor + n + 12);
        const auto observedFirst = static_cast<unsigned>(d.sequences[3].values.size());
        for (unsigned a = 0; a < active.size(); ++a)
            d.sequences[3].values.push_back(complexValue(spectrum[idx(active[a])], window, s, active[a]));
        std::vector<std::pair<double, double>> phases;
        const auto pilotFirst = static_cast<unsigned>(d.sequences[4].values.size());
        double prior = 0;
        for (unsigned a = 0; a < pilots.size(); ++a) {
            const auto k = idx(pilots[a]);
            const auto h = best.channel[k];
            const bool valid = std::norm(h) > std::max(fade, best.noise * 4);
            const auto z = valid ? spectrum[k] * std::conj(h) / (std::norm(h) + regularizer) : C{};
            double phase = std::arg(z * std::conj(point(pilotValues[a], order)));
            if (valid) {
                if (!phases.empty()) {
                    while (phase - prior > pi)
                        phase -= 2 * pi;
                    while (phase - prior < -pi)
                        phase += 2 * pi;
                }
                prior = phase;
                phases.emplace_back(pilots[a], phase);
            }
            auto v = complexValue(z, {}, s, pilots[a], valid);
            const auto aidx =
                static_cast<unsigned>(std::lower_bound(active.begin(), active.end(), pilots[a]) - active.begin());
            v.inputs = {{0, 0, 1}, {1, aidx, 1}, {3, 0, observedFirst + static_cast<unsigned>(active.size())}};
            d.sequences[4].values.push_back(v);
        }
        double slope = 0, phase = 0;
        if (phases.size() >= 2) {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (auto [k, v] : phases) {
                sx += k;
                sy += v;
                sxx += k * k;
                sxy += k * v;
            }
            const double m = phases.size();
            slope = (sxy - sx * sy / m) / (sxx - sx * sx / m);
            phase = (sy - slope * sx) / m;
        }
        double pilotError = 0;
        for (auto [k, v] : phases)
            pilotError += std::pow(v - phase - slope * k, 2);
        const bool pilotLock = phases.size() >= 2 && std::abs(slope) < pi / 2 && pilotError / phases.size() < .12;
        for (unsigned a = 0; a < active.size(); ++a) {
            if (std::binary_search(pilots.begin(), pilots.end(), active[a]))
                continue;
            const auto k = idx(active[a]);
            const auto h = best.channel[k];
            const bool valid = pilotLock && std::norm(h) > std::max(fade, best.noise * 4);
            const auto z = valid ? spectrum[k] * std::conj(h) / (std::norm(h) + regularizer) *
                                       std::polar(1., -phase - slope * active[a])
                                 : C{};
            const auto ref = reference.empty() ? point(slice(z, order), order) : point(reference[dataIndex], order);
            auto v = complexValue(z, {}, s, active[a], valid);
            v.metric = std::norm(h) / meanH;
            v.inputs = {
                {0, 0, 1}, {1, a, 1}, {3, observedFirst + a, 1}, {4, pilotFirst, static_cast<unsigned>(pilots.size())}};
            d.sequences[5].values.push_back(v);
            auto r = complexValue(ref, {}, s, active[a], valid);
            r.inputs = {{5, dataIndex, 1}};
            d.sequences[6].values.push_back(r);
            evm.add(z, ref, valid);
            ++dataIndex;
        }
        // Phase slope is delay in samples. Track the next FFT window with a
        // bounded second-order loop; correction support includes all earlier
        // pilots because their state affects subsequent sampling.
        const auto delay = slope * n / (2 * pi);
        if (haveSlope)
            drift = std::clamp(.8 * drift - .2 * (slope - lastSlope) * n / (2 * pi), -.1, .1);
        sampling = std::clamp(sampling - .5 * delay + drift, -double(cp) / 4, double(cp) / 4);
        lastSlope = slope;
        haveSlope = true;
        w.tick(active.size() * 40);
    }
    d.method["tracking"] = {
        {"fft_offset_samples", sampling}, {"clock_step_samples", drift}, {"pilot_slope_rad_per_bin", lastSlope}};
    d.method["evm"] = evm.json(reference.empty() ? "decision-directed" : "known-data");
    d.stages[2].features["evm"] = d.method["evm"];
    d.stages[2].features["tracking"] = d.method["tracking"];
    pack(d, 5, order, w);
    finish(d, w, (x.capacity() + n * 8) * sizeof(C) + scores.capacity() * sizeof(std::pair<double, unsigned>) + 65536);
    return d;
}
} // namespace
RecoveryProvider ofdm() {
    return {"trained-ofdm-receiver", "recovery-ofdm", "label", "about", 2, 1, describe, recover};
}
} // namespace signalscope::recovery
