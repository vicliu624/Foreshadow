#include "native_test_main.hpp"
#include "plugins/wspr/live_panel.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/signal_source.hpp"
#include <QApplication>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <numbers>
using namespace signalscope;
namespace {
struct Lease final : ReceptionLease {
    void release() noexcept override {}
};
} // namespace
static int run(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc < 2)
        throw std::runtime_error("Expected reference path");
    std::ifstream file(path_from_utf8(argv[1]));
    Json reference;
    file >> reference;
    const auto tones = reference.at("cases")[0].at("symbols").get<std::string>();
    SignalWindow source;
    source.stream = {375, 14097100, SampleFormat::cf32_le};
    source.source_id = "wspr-panel-reference";
    source.recording_origin = 10000;
    SignalSegment segment;
    segment.first_sample = 10000;
    segment.samples.resize(45000);
    double phase = .37;
    for (unsigned n = 0; n < 162 * 256; ++n) {
        segment.samples[n + 750] = std::polar(.3f, float(phase));
        phase +=
            2 * std::numbers::pi * (17.3 + (tones[n / 256] - '0') * 375. / 256 + 2. * n / (162 * 256)) / 375;
    }
    source.segments.push_back(std::move(segment));
    const auto root =
        std::filesystem::temp_directory_path() /
        path_from_utf8("foreshadow-wspr-panel-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto metadata = write_signal_archive(root, source);
    const auto language = argc > 3 ? QString::fromUtf8(argv[3]) : QStringLiteral("en");
    auto tool = desktop::wsprLiveProvider().create(language, nullptr);
    if ((language == "zh-CN" && tool.widget->windowTitle() != QString::fromUtf8("WSPR 接收与解析")) ||
        (language == "fr-CA" && tool.widget->windowTitle() != "WSPR reception and decoding"))
        throw std::runtime_error("WSPR language resource fallback mismatch");
    ReceptionConsumer captured;
    tool.attachReception(
        [&](const ReceptionDemand &demand, ReceptionConsumer consumer) -> std::shared_ptr<ReceptionLease> {
            if (demand.frequency_hz != 14097100)
                throw std::runtime_error("WSPR panel requested wrong RF center");
            ReceptionStatus status("receiving");
            status.stream = source.stream;
            captured = consumer;
            consumer.changed(status);
            const auto stride = bytes_per_pair(source.stream.format);
            Bytes bytes(source.segments[0].samples.size() * stride);
            for (unsigned n = 0; n < 45000; ++n)
                write_iq_pair(bytes.data() + n * stride, source.stream.format, source.segments[0].samples[n]);
            consumer.offer({bytes, 10000, source.stream, false});
            return std::make_shared<Lease>();
        });
    tool.widget->resize(800, 600);
    tool.widget->show();
    auto *table = tool.widget->findChild<QTableWidget *>("wspr-results");
    auto *start = tool.widget->findChild<QPushButton *>("wspr-start");
    if (!table || !start)
        throw std::runtime_error("WSPR panel controls unavailable");
    start->click();
    unsigned stage = 0;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        if (stage < 2 && (table->rowCount() != 1 || table->item(0, 0)->text() != "K1ABC"))
            return;
        if (stage == 0) {
            tool.stop();
            captured.changed(ReceptionStatus("receiving"));
            if (tool.snapshot().at("reception").at("state") != "stopped") {
                app.exit(6);
                return;
            }
            desktop::ResearchRecordingContext context;
            context.metadata = metadata;
            context.event = {{"future_opaque", {{"value", 17}}}};
            const auto actions = tool.recordingActions(context);
            if (actions.empty() || actions[0].reveal.empty()) {
                app.exit(2);
                return;
            }
            stage = 1;
            actions[0].trigger();
        } else if (stage == 1 && !tool.busy()) {
            if (tool.snapshot().value("event", Json::object()) != Json{{"future_opaque", {{"value", 17}}}}) {
                app.exit(4);
                return;
            }
            if (argc > 2 && argv[2][0] && !tool.widget->grab().save(QString::fromUtf8(argv[2]))) {
                app.exit(3);
                return;
            }
            desktop::ResearchRecordingContext missing;
            missing.metadata = root / "missing.amc-window.json";
            stage = 2;
            tool.recordingActions(missing).at(0).trigger();
        } else if (stage == 2 && !tool.busy()) {
            poll.stop();
            if (!tool.health || tool.health().isEmpty() ||
                tool.snapshot().at("state") != "recording_failed") {
                app.exit(5);
                return;
            }
            std::cout
                << "WSPR live panel, recording action, opaque context, late status and error health passed\n";
            app.exit(0);
        }
    });
    poll.start(100);
    QTimer::singleShot(45000, &app, [&] {
        std::cerr << tool.snapshot().dump() << '\n';
        app.exit(1);
    });
    const auto result = app.exec();
    tool.stop();
    delete tool.widget;
    if (root.parent_path() == std::filesystem::temp_directory_path() &&
        path_to_utf8(root.filename()).starts_with("foreshadow-wspr-panel-")) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    return result;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
