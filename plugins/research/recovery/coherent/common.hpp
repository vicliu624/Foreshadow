#pragma once
#include "signalscope/backend/dsp/recovery.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <numeric>
#include <span>

namespace signalscope::recovery::coherent {
using C = std::complex<double>;
inline constexpr double pi = std::numbers::pi;
struct Work {
    const RecoveryContext &context;
    std::stop_token stop;
    std::uint64_t operations = 0;
    mutable std::size_t peak_workspace = 0;
    void tick(std::uint64_t count = 1) {
        if (count > context.budget.maximum_operations - operations)
            throw std::length_error("Coherent receiver operation budget exceeded");
        operations += count;
        if (stop.stop_requested())
            throw std::runtime_error("Recovery canceled");
    }
    void memory(std::size_t bytes) const {
        peak_workspace = std::max(peak_workspace, bytes);
        if (bytes > context.budget.workspace_payload_bytes)
            throw std::length_error("Coherent receiver workspace budget exceeded");
    }
};
inline Json number(const char *key, double value, double low, double high) {
    return {{"key", key},       {"type", "number"}, {"extension", true}, {"label_key", key},
            {"default", value}, {"minimum", low},   {"maximum", high}};
}
inline Json integer(const char *key, unsigned value, unsigned low, unsigned high) {
    auto f = number(key, value, low, high);
    f["type"] = "integer";
    f["default"] = value;
    return f;
}
inline Json text(const char *key, const std::string &value) {
    return {{"key", key},       {"type", "string"}, {"extension", true},
            {"label_key", key}, {"default", value}, {"maximum_length", 16384}};
}
inline Json choice(const char *key, const char *value, std::initializer_list<const char *> choices) {
    auto f = Json{{"key", key},
                  {"type", "enum"},
                  {"extension", true},
                  {"label_key", key},
                  {"default", value},
                  {"choices", Json::array()},
                  {"choice_labels", Json::object()}};
    for (auto c : choices) {
        f["choices"].push_back(c);
        f["choice_labels"][c] = {{"label_key", std::string(key) + "." + c}};
    }
    return f;
}
inline void issue(RecoveryData &d, const std::string &field, const char *key = "issue.parameter") {
    d.issues.push_back({{"field", field}, {"label_key", key}});
}
inline bool parameters(RecoveryData &d, const Json &supplied, const Json &fields) {
    for (const auto &f : fields) {
        const auto key = f.at("key").get<std::string>();
        auto v = supplied.value(key, f.at("default"));
        d.parameters[key] = v;
        const auto t = f.at("type").get<std::string>();
        bool ok = false;
        if (t == "string")
            ok = v.is_string() && v.get_ref<const std::string &>().size() <= 16384;
        else if (t == "enum")
            ok = std::find(f.at("choices").begin(), f.at("choices").end(), v) != f.at("choices").end();
        else
            ok = v.is_number() && (t != "integer" || v.is_number_integer()) && std::isfinite(v.get<double>()) &&
                 v >= f.at("minimum") && v <= f.at("maximum");
        if (!ok)
            issue(d, key);
    }
    for (const auto &[k, v] : supplied.items()) {
        (void)v;
        if (!d.parameters.contains(k))
            issue(d, k, "issue.unknown");
    }
    return d.issues.empty();
}
inline std::vector<unsigned> symbols(const std::string &text, unsigned order, std::size_t maximum = 4096) {
    std::vector<unsigned> out;
    std::string_view s = text;
    while (!s.empty()) {
        while (!s.empty() && (s.front() == ',' || s.front() == ' ' || s.front() == '\n' || s.front() == '\t'))
            s.remove_prefix(1);
        if (s.empty())
            break;
        unsigned v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{} || r.ptr == s.data() || v >= order || out.size() >= maximum)
            throw std::invalid_argument("Invalid training/reference symbol sequence");
        s.remove_prefix(static_cast<std::size_t>(r.ptr - s.data()));
        if (!s.empty() && s.front() != ',' && s.front() != ' ' && s.front() != '\n' && s.front() != '\t')
            throw std::invalid_argument("Invalid symbol separator");
        out.push_back(v);
    }
    return out;
}
inline C point(unsigned value, unsigned order) { return std::polar(1., 2 * pi * value / order); }
inline unsigned slice(C value, unsigned order) {
    auto angle = std::arg(value) * order / (2 * pi);
    auto at = static_cast<int>(std::llround(angle));
    return static_cast<unsigned>((at % int(order) + int(order)) % int(order));
}
inline std::string training(unsigned order, unsigned count = 64) {
    std::string s;
    std::uint32_t state = 0x9e3779b9;
    for (unsigned n = 0; n < count; ++n) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        if (n)
            s += ' ';
        s += std::to_string(state % order);
    }
    return s;
}
inline void fft(std::vector<C> &a, bool inverse, Work &w) {
    const auto n = a.size();
    if (n < 2 || (n & (n - 1)))
        throw std::invalid_argument("FFT length is not a power of two");
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        w.tick(n * 20);
        const auto step = std::polar(1., (inverse ? 2 : -2) * pi / len);
        for (std::size_t i = 0; i < n; i += len) {
            C phase = 1;
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto x = a[i + j], y = a[i + j + len / 2] * phase;
                a[i + j] = x + y;
                a[i + j + len / 2] = x - y;
                phase *= step;
            }
        }
    }
    const auto scale = 1 / std::sqrt(double(n));
    for (auto &z : a)
        z *= scale;
}
inline std::vector<C> input(const RecoveryContext &c, Work &w) {
    if (c.range.count > 65536)
        throw std::length_error("Select at most 65536 coherent receiver input positions");
    w.memory(c.range.count * sizeof(C) * 3 + 65536);
    const auto &a = *c.analysis;
    const auto &i = a.trace("i");
    const auto &q = a.trace("q");
    validate_analysis_trace(a, i);
    validate_analysis_trace(a, q);
    if (analysis_trace_spans(a, i) != a.spans || analysis_trace_spans(a, q) != a.spans)
        throw std::invalid_argument("Receiver requires full I/Q coverage");
    std::vector<C> out;
    out.reserve(static_cast<std::size_t>(c.range.count));
    std::uint64_t next = c.range.first;
    for (const auto &span : a.spans) {
        const auto first = std::max(next, span.first_sample),
                   end = std::min(c.range.first + c.range.count, span.first_sample + span.count);
        if (first >= end)
            continue;
        if (first != next || (first != c.range.first && first == span.first_sample && span.discontinuous_before))
            throw std::invalid_argument("Receiver selection crosses missing or discontinuous observations");
        for (auto p = first; p < end; ++p) {
            const auto at = span.first_value + static_cast<std::size_t>(p - span.first_sample);
            out.emplace_back(i.values[at], q.values[at]);
        }
        next = end;
    }
    if (next != c.range.first + c.range.count)
        throw std::invalid_argument("Receiver selection contains missing observations");
    w.tick(out.size());
    return out;
}
inline C interpolate(const std::vector<C> &x, double at) {
    if (at < 0 || at + 1 >= x.size())
        throw std::out_of_range("Receiver interpolation outside input");
    const auto n = static_cast<std::size_t>(at);
    return x[n] + (x[n + 1] - x[n]) * (at - n);
}
inline SignalSampleRange support(const RecoveryContext &c, double first, double last) {
    const auto lo = static_cast<std::uint64_t>(std::max(0., std::floor(first))),
               hi = static_cast<std::uint64_t>(std::min(double(c.range.count), std::ceil(last) + 1));
    if (hi <= lo)
        throw std::invalid_argument("Empty receiver support");
    return {c.range.first + lo, hi - lo};
}
// Small, pivoted complex normal-equation solve. Ridge and fit domain are
// reported by the caller; this is regularized LS, not a claimed RF calibration.
inline std::vector<C> leastSquares(const std::vector<std::vector<C>> &rows, const std::vector<C> &target, double ridge,
                                   Work &w) {
    if (rows.empty() || rows.size() != target.size())
        throw std::invalid_argument("Empty least-squares system");
    const auto n = rows.front().size();
    std::vector<std::vector<C>> a(n, std::vector<C>(n + 1));
    double trace = 0;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        w.tick(n * n * 8);
        for (std::size_t j = 0; j < n; ++j) {
            a[j][n] += std::conj(rows[r][j]) * target[r];
            for (std::size_t k = 0; k < n; ++k)
                a[j][k] += std::conj(rows[r][j]) * rows[r][k];
        }
    }
    for (std::size_t j = 0; j < n; ++j)
        trace += a[j][j].real();
    if (trace < 1e-15)
        throw std::runtime_error("Singular training system");
    for (std::size_t j = 0; j < n; ++j)
        a[j][j] += ridge * trace / n;
    for (std::size_t j = 0; j < n; ++j) {
        auto pivot = j;
        for (auto k = j + 1; k < n; ++k)
            if (std::abs(a[k][j]) > std::abs(a[pivot][j]))
                pivot = k;
        if (std::abs(a[pivot][j]) < trace * 1e-10)
            throw std::runtime_error("Ill-conditioned training system");
        std::swap(a[j], a[pivot]);
        const auto d = a[j][j];
        for (auto k = j; k <= n; ++k)
            a[j][k] /= d;
        for (std::size_t r = 0; r < n; ++r)
            if (r != j) {
                const auto z = a[r][j];
                for (auto k = j; k <= n; ++k)
                    a[r][k] -= z * a[j][k];
            }
    }
    std::vector<C> out(n);
    for (std::size_t j = 0; j < n; ++j)
        out[j] = a[j][n];
    return out;
}
inline RecoverySequence plane(const char *id, const char *key, unsigned stage, const char *domain,
                              bool reference = false) {
    RecoverySequence s{id, key, stage, RecoveryValueKind::complex, {}};
    s.domain_key = domain;
    s.reference = reference;
    return s;
}
inline RecoveryValue complexValue(C z, SignalSampleRange range, unsigned group, int coordinate, bool known = true) {
    RecoveryValue v;
    v.known = known;
    v.complex_value = z;
    v.support = range;
    v.group = group;
    v.coordinate = coordinate;
    if (!known)
        v.issue_key = "issue.erasure";
    return v;
}
inline void noLock(RecoveryData &d, const char *reason, Work &w) {
    d.kind = RecoveryKind::measurement;
    d.method["synchronization"]["locked"] = false;
    d.method["synchronization"]["label_key"] = reason;
    d.method["evm"] = {{"available", false}, {"label_key", "evm.unavailable"}};
    d.metrics = {{"evm-rms", "metric.evm_rms", "unit.percent", 0, false, reason}};
    d.operations = w.operations;
    d.workspace_payload_bytes = w.peak_workspace;
}
struct Evm {
    double error = 0, power = 0, peak = 0;
    unsigned count = 0, excluded = 0;
    void add(C z, C r, bool valid) {
        if (!valid) {
            ++excluded;
            return;
        }
        const auto e = std::norm(z - r);
        error += e;
        power += std::norm(r);
        peak = std::max(peak, e);
        ++count;
    }
    Json json(const char *source) const {
        Json j = {{"available", count >= 8 && power > 1e-15 && excluded <= count / 4},
                  {"reference_source", source},
                  {"normalization", "mean-reference-power"},
                  {"measurement_point", "after-equalization-and-phase-correction"},
                  {"count", count},
                  {"excluded", excluded},
                  {"training_and_pilots_excluded", true}};
        if (count >= 8 && power > 1e-15 && excluded <= count / 4) {
            j["rms_percent"] = 100 * std::sqrt(error / power);
            j["peak_percent"] = 100 * std::sqrt(peak / (power / count));
            j["reference_power"] = power / count;
        } else
            j["label_key"] = "evm.unavailable";
        return j;
    }
};
inline void pack(RecoveryData &d, unsigned points, unsigned order, Work &w) {
    const auto bitsPer = order == 2 ? 1u : 2u;
    const auto decision = static_cast<unsigned>(d.sequences.size());
    d.sequences.push_back(
        {"decisions", "sequence.decisions", static_cast<unsigned>(d.stages.size() - 1), RecoveryValueKind::symbol, {}});
    d.sequences.push_back(
        {"bits", "sequence.bits", static_cast<unsigned>(d.stages.size() - 1), RecoveryValueKind::bit, {}});
    d.sequences.push_back(
        {"bytes", "sequence.bytes", static_cast<unsigned>(d.stages.size() - 1), RecoveryValueKind::byte, {}});
    const auto count = d.sequences[points].values.size();
    auto &decisions = d.sequences[decision].values;
    auto &bits = d.sequences[decision + 1].values;
    auto &bytes = d.sequences[decision + 2].values;
    decisions.reserve(count);
    bits.reserve(count * bitsPer);
    bytes.reserve((count * bitsPer + 7) / 8);
    for (unsigned n = 0; n < count; ++n) {
        const auto &p = d.sequences[points].values[n];
        RecoveryValue v;
        v.known = p.known;
        v.value = slice(p.complex_value, order);
        if (!v.known)
            v.issue_key = "issue.erasure";
        v.inputs = {{points, n, 1}};
        decisions.push_back(v);
        for (unsigned b = 0; b < bitsPer; ++b) {
            RecoveryValue bit;
            bit.known = v.known;
            bit.value = (v.value >> (bitsPer - 1 - b)) & 1;
            bit.issue_key = v.issue_key;
            bit.inputs = {{decision, n, 1}};
            bits.push_back(std::move(bit));
        }
    }
    for (unsigned n = 0; n < bits.size(); n += 8) {
        RecoveryValue v;
        v.known = n + 8 <= bits.size();
        const auto len = std::min<unsigned>(8, static_cast<unsigned>(bits.size()) - n);
        for (unsigned b = 0; b < len; ++b) {
            v.value |= bits[n + b].value << (7 - b);
            v.known = v.known && bits[n + b].known;
        }
        if (!v.known)
            v.issue_key = "issue.erasure";
        v.inputs = {{decision + 1, n, len}};
        bytes.push_back(std::move(v));
    }
    if (!bytes.empty())
        d.frames.push_back({"payload",
                            "frame.payload",
                            "frame.scope",
                            decision + 2,
                            0,
                            static_cast<unsigned>(bytes.size()),
                            RecoveryFrameState::candidate,
                            {},
                            {{"mapping", "natural-phase-index-msb-first"}, {"checksum", "not-performed"}}});
    w.tick(bits.size() * 8);
}
inline void finish(RecoveryData &d, Work &w, std::size_t workspace) {
    const auto &evm = d.method.at("evm");
    const bool ok = evm.at("available");
    d.metrics = {
        {"cfo", "metric.cfo", "unit.hz", d.method.at("synchronization").value("cfo_hz", 0.), true, ""},
        {"evm-rms", evm.at("reference_source") == "known-data" ? "metric.evm_rms.known" : "metric.evm_rms.decision",
         "unit.percent", evm.value("rms_percent", 0.), ok, ok ? "" : "evm.unavailable"},
        {"evm-peak", "metric.evm_peak", "unit.percent", evm.value("peak_percent", 0.), ok, ok ? "" : "evm.unavailable"},
        {"evm-count", "metric.count", "unit.points", evm.at("count"), true, ""},
        {"evm-excluded", "metric.excluded", "unit.points", evm.at("excluded"), true, ""}};
    d.operations = w.operations;
    w.memory(workspace);
    d.workspace_payload_bytes = w.peak_workspace;
    std::size_t values = 0, links = 0;
    for (const auto &s : d.sequences) {
        values += s.values.size();
        for (const auto &v : s.values)
            links += v.inputs.size();
    }
    if (values > w.context.budget.maximum_values || links > w.context.budget.maximum_links ||
        d.payload_bytes() > w.context.budget.result_payload_bytes)
        throw std::length_error("Coherent receiver result budget exceeded");
}
} // namespace signalscope::recovery::coherent
