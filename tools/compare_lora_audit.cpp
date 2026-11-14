#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include "offline_detect.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Json = nlohmann::json;

Json readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    const auto bytes = file.readAll();
    return Json::parse(bytes.begin(), bytes.end());
}

std::string sha256(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        throw std::runtime_error(file.errorString().toStdString());
    return hash.result().toHex().toStdString();
}

std::uint64_t startSample(const Json& event) {
    return event.at("sfd_start_sample").get<std::uint64_t>();
}

Json pairEvents(Json reference, const Json& observed, double rate) {
    std::stable_sort(reference.begin(), reference.end(), [](const auto& a, const auto& b) {
        return startSample(a) < startSample(b);
    });
    std::vector<bool> matched(observed.size());
    auto pairs = Json::array(), missing = Json::array(), additional = Json::array();
    for (const auto& expected : reference) {
        const auto bandwidth = expected.at("bandwidth_hz").get<double>();
        const auto sf = expected.at("sf_candidate").get<int>();
        if (!(bandwidth > 0) || sf < 5 || sf > 12)
            throw std::runtime_error("Invalid LoRa event bandwidth or spreading factor");
        const auto sample = startSample(expected);
        const auto center = expected.at("center_frequency_hz").get<double>();
        const auto binHz = std::ldexp(bandwidth, -sf);
        std::size_t count = 0, selected = 0;
        for (std::size_t i = 0; i < observed.size(); ++i) {
            if (matched[i]) continue;
            const auto& event = observed[i];
            if (event.at("sf_candidate") != sf || event.at("bandwidth_hz") != bandwidth ||
                event.at("iq_inverted") != expected.at("iq_inverted")) continue;
            const auto other = startSample(event);
            const auto timing = std::max(sample, other) - std::min(sample, other);
            const auto frequency = std::abs(event.at("center_frequency_hz").get<double>() - center);
            if (timing <= 4 * rate / bandwidth && frequency <= 2 * binHz) {
                ++count;
                selected = i;
            }
        }
        if (count == 1) {
            matched[selected] = true;
            pairs.push_back({{"reference", expected}, {"observed", observed[selected]}});
        } else {
            missing.push_back({{"reference", expected}, {"ambiguous_matches", count}});
        }
    }
    for (std::size_t i = 0; i < observed.size(); ++i)
        if (!matched[i]) additional.push_back(observed[i]);
    return {{"matched", pairs}, {"missing", missing}, {"additional", additional}};
}

Json detect(const QString& executable, QStringList arguments, bool dense) {
    arguments.append({"--dense-short-symbols", dense ? "1" : "0"});
    return signalscope::tools::runOfflineDetect(executable,arguments);
}

Json compareCapture(const QString& executable, const QString& metadata, const QString& liveStatus) {
    const auto status = readJson(liveStatus);
    const auto meta = readJson(metadata);
    const auto& info = meta.at("global");
    const auto& receiver = status.at("receiver_configuration");
    const QFileInfo recording(metadata);
    const auto data = recording.absolutePath() + '/' + recording.completeBaseName() + ".sigmf-data";
    const auto rate = info.at("core:sample_rate").get<double>();
    const auto pairs = status.at("source_pairs").get<std::uint64_t>();
    const QFileInfo samples(data);
    if (status.at("state") != "stopped" || info.at("signalscope:status") != "complete" ||
        info.at("core:datatype") != "cu8" || info.at("signalscope:result").at("stream_first_sample") != 0 ||
        !samples.isFile() || samples.size() % 2 != 0 || static_cast<std::uint64_t>(samples.size() / 2) != pairs ||
        info.at("signalscope:sample_count") != pairs || receiver.at("sample_rate_hz") != rate ||
        meta.at("captures").at(0).at("core:frequency") != receiver.at("frequency_hz"))
        throw std::runtime_error("Audit recording is incomplete or does not match the live acquisition");

    const auto& cfg = status.at("detector");
    auto widths = cfg.at("bandwidth_candidates_hz").get<std::vector<double>>();
    std::sort(widths.begin(), widths.end());
    QString bandwidth;
    if (widths.size() == 1) bandwidth = QString::number(widths.front(), 'g', 17);
    else if (widths == std::vector<double>{125000, 250000, 500000}) bandwidth = "all";
    else throw std::runtime_error("Expected one bandwidth or the standard three bandwidths");
    const auto normal = cfg.at("normal_iq").get<bool>();
    const auto inverted = cfg.at("inverted_iq").get<bool>();
    if (!normal && !inverted) throw std::runtime_error("The live detector had no enabled I/Q polarity");
    const auto numeric = [](const Json& number) { return QString::fromStdString(number.dump()); };
    const QStringList arguments{
        "detect", "--input", recording.absoluteFilePath(), "--bw", bandwidth,
        "--sf-min", numeric(cfg.at("sf_min")), "--sf-max", numeric(cfg.at("sf_max")),
        "--iq-mode", normal ? (inverted ? "both" : "normal") : "inverted",
        "--workers", numeric(cfg.at("workers")), "--threshold-db", numeric(cfg.at("preamble_threshold_db")),
        "--time-phases", numeric(cfg.at("time_phases")), "--probe-stride", QString::number(cfg.value("probe_stride_symbols", 4)),
        "--search-width", numeric(receiver.at("search_width_hz"))
    };
    const auto& live = status.at("recent_lora_events");
    if (!live.is_array()) throw std::runtime_error("Expected a live LoRa event list");
    auto results = Json::object();
    for (const auto dense : {false, true}) {
        const auto result = detect(executable, arguments, dense);
        auto events = Json::array();
        for (const auto& event : result.at("events"))
            if (event.at("evidence_level") == "lora_structure_match") events.push_back(event);
        results[dense ? "dense" : "default"] = {
            {"events", events}, {"against_live", pairEvents(live, events, rate)},
            {"result_limit_reached", result.at("results_at_limit")}
        };
    }
    auto drops = Json::object();
    for (const auto* key : {"analysis_dropped_pairs", "analysis_gaps", "result_dropped_events"})
        drops[key] = status.at(key);
    return {
        {"input_sha256", sha256(data)}, {"offline_binary_sha256", sha256(executable)},
        {"source_pairs", pairs}, {"live_events", live}, {"live_recent_event_limit_may_apply", live.size() >= 32},
        {"live_drops", drops}, {"live_dense_short_symbols", cfg.value("dense_short_symbols", false)},
        {"offline", results}
    };
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Compare live, default offline, and dense offline LoRa results for one audit recording.");
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("input"), QStringLiteral("Audit recording metadata."), QStringLiteral("path")},
        {QStringLiteral("live-status"), QStringLiteral("Live monitor status JSON."), QStringLiteral("path")},
        {QStringLiteral("exe"), QStringLiteral("Offline signalscope executable."), QStringLiteral("path")},
        {QStringLiteral("output"), QStringLiteral("New comparison JSON file."), QStringLiteral("path")}
    });
    parser.process(app);
    try {
        for (const auto* option : {"input", "live-status", "exe", "output"})
            if (parser.value(option).isEmpty()) throw std::runtime_error(std::string("Missing --") + option);
        if (!parser.positionalArguments().isEmpty()) throw std::runtime_error("Unexpected positional arguments");
        const auto executable = QFileInfo(parser.value("exe")).absoluteFilePath();
        const auto report = compareCapture(executable, parser.value("input"), parser.value("live-status")).dump(2) + '\n';
        QFile output(parser.value("output"));
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            throw std::runtime_error(output.errorString().toStdString());
        if (output.write(report.data(), static_cast<qint64>(report.size())) != static_cast<qint64>(report.size()) || !output.flush()) {
            const auto detail = output.errorString().toStdString();
            output.remove();
            throw std::runtime_error(detail);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
