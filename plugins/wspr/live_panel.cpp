#include "live_panel.hpp"
#include "live_input.hpp"
#include "recording.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <thread>
#include <tuple>
namespace signalscope::desktop {
namespace {
class Panel final : public QWidget {
  public:
    Panel(QString language, QWidget *parent)
        : QWidget(parent), language_(std::move(language)), catalog_(TextCatalog::resource("wspr")) {
        setObjectName("wspr-panel");
        setWindowTitle(text("title"));
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        frequency_ = new QDoubleSpinBox;
        frequency_->setObjectName("wspr-frequency");
        frequency_->setDecimals(6);
        frequency_->setRange(.001, 6000);
        frequency_->setValue(14.0971);
        form->addRow(text("frequency"), frequency_);
        auto *band = new QWidget;
        auto *bandLayout = new QHBoxLayout(band);
        bandLayout->setContentsMargins(0, 0, 0, 0);
        minimum_ = new QDoubleSpinBox;
        maximum_ = new QDoubleSpinBox;
        for (auto *field : {minimum_, maximum_}) {
            field->setRange(-150, 150);
            field->setDecimals(1);
            bandLayout->addWidget(field);
        }
        minimum_->setValue(-110);
        maximum_->setValue(110);
        form->addRow(text("search_range"), band);
        drift_ = new QSpinBox;
        drift_->setRange(0, 8);
        drift_->setValue(4);
        form->addRow(text("drift_limit"), drift_);
        layout->addLayout(form);
        auto *buttons = new QHBoxLayout;
        auto *start = new QPushButton(text("start"));
        start->setObjectName("wspr-start");
        auto *stopButton = new QPushButton(text("stop"));
        stopButton->setObjectName("wspr-stop");
        auto *open = new QPushButton(text("open_recording"));
        open->setObjectName("wspr-open-recording");
        buttons->addWidget(start);
        buttons->addWidget(stopButton);
        buttons->addWidget(open);
        layout->addLayout(buttons);
        auto *hint = new QLabel(text("hint"));
        hint->setWordWrap(true);
        layout->addWidget(hint);
        status_ = new QLabel(text("stopped"));
        status_->setWordWrap(true);
        layout->addWidget(status_);
        metrics_ = new QLabel;
        metrics_->setWordWrap(true);
        layout->addWidget(metrics_);
        table_ = new QTableWidget(0, 7);
        table_->setObjectName("wspr-results");
        table_->setHorizontalHeaderLabels({text("call"), text("grid"), text("power"), text("base_frequency"),
                                           text("drift"), text("sync"), text("source_sample")});
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(table_, 1);
        connect(start, &QPushButton::clicked, this, [this] { listen(); });
        connect(stopButton, &QPushButton::clicked, this, [this] { stop(); });
        timer_.setInterval(500);
        connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
        timer_.start();
        connect(open, &QPushButton::clicked, this, [this] {
            const auto file = QFileDialog::getOpenFileName(this, text("open_recording"));
            if (!file.isEmpty())
                readRecording(path_from_utf8(file.toUtf8().toStdString()), {});
        });
    }
    ~Panel() override {
        ++generation_;
        lease_.reset();
        if (input_)
            input_->stop();
        recording_worker_.request_stop();
        if (recording_worker_.joinable())
            recording_worker_.join();
    }
    LiveResearchTool tool() {
        LiveResearchTool tool;
        tool.widget = this;
        tool.monitorBound = false;
        tool.attachReception = [this](ReceptionAcquire acquire) { acquire_ = std::move(acquire); };
        tool.stop = [this] { stop(); };
        tool.busy = [this] { return bool(lease_) || (recording_ && recording_->active.load()); };
        tool.snapshot = [this] { return diagnostic(); };
        tool.health = [this] {
            if (recording_ && recording_->error.load())
                return text("recording_failed");
            if (input_ && !input_->snapshot().error.empty())
                return text("processing_error");
            return reception_error_.empty() ? QString() : text("unavailable");
        };
        tool.summary = [this] { return lease_ || recording_ ? status_->text() : QString(); };
        tool.recordingActions = [this](const ResearchRecordingContext &context) {
            LiveResearchAction action;
            action.id = "wspr.decode-recording";
            action.label = text("decode_recording");
            action.reveal = {this};
            action.trigger = [this, context] {
                std::optional<SignalSampleRange> range;
                if (context.range)
                    range = SignalSampleRange{context.range->first, context.range->count};
                readRecording(context.metadata, range, context.event);
            };
            return std::vector<LiveResearchAction>{std::move(action)};
        };
        return tool;
    }

  private:
    QString text(const std::string &key, const Json &arguments = Json::object()) const {
        Json node = {{"label_key", key}, {"arguments", arguments}};
        catalog_.resolve(node);
        return localizedText(node.at("label"), language_, QString::fromStdString(key));
    }
    void controls(bool enabled) {
        frequency_->setEnabled(enabled);
        minimum_->setEnabled(enabled);
        maximum_->setEnabled(enabled);
        drift_->setEnabled(enabled);
    }
    void stop() {
        ++generation_;
        lease_.reset();
        if (input_)
            input_->stop();
        recording_worker_.request_stop();
        if (recording_worker_.joinable())
            recording_worker_.join();
        recording_.reset();
        input_.reset();
        render_stamp_.reset();
        reception_ = ReceptionStatus("stopped");
        reception_error_.clear();
        controls(true);
        status_->setText(text("stopped"));
    }
    void readRecording(const std::filesystem::path &path, std::optional<SignalSampleRange> range,
                       Json event = Json::object()) {
        stop();
        if (minimum_->value() >= maximum_->value()) {
            status_->setText(text("invalid_range"));
            return;
        }
        wspr::SearchOptions options;
        options.minimum_hz = minimum_->value();
        options.maximum_hz = maximum_->value();
        options.maximum_drift_hz = unsigned(drift_->value());
        const auto target = std::round(frequency_->value() * 1e6);
        recording_ = std::make_shared<RecordingState>();
        auto state = recording_;
        state->active = true;
        state->metadata = path;
        state->event = std::move(event);
        table_->setRowCount(0);
        status_->setText(text("recording_busy"));
        controls(false);
        try {
            recording_worker_ = std::jthread([state, path, target, options, range](std::stop_token stop) {
                try {
                    state->result.store(std::make_shared<const wspr::RecordingResult>(
                        wspr::decode_recording(path, target, options, range, stop)));
                } catch (const std::exception &error) {
                    state->error.store(std::make_shared<const std::string>(error.what()));
                }
                state->active = false;
            });
        } catch (const std::exception &error) {
            state->error.store(std::make_shared<const std::string>(error.what()));
            state->active = false;
            controls(true);
            status_->setText(text("recording_failed"));
        }
    }
    void listen() {
        stop();
        if (!acquire_) {
            status_->setText(text("unavailable"));
            return;
        }
        if (minimum_->value() >= maximum_->value()) {
            status_->setText(text("invalid_range"));
            return;
        }
        wspr::SearchOptions options;
        options.minimum_hz = minimum_->value();
        options.maximum_hz = maximum_->value();
        options.maximum_drift_hz = unsigned(drift_->value());
        const auto target = std::round(frequency_->value() * 1e6);
        try {
            input_ = std::make_shared<wspr::LiveInput>(target, options);
            auto input = input_;
            const auto generation = generation_;
            QPointer<Panel> guard(this);
            lease_ = acquire_({target, 375, 375, 20000000, {}},
                              {[guard, generation](const ReceptionStatus &status) {
                                   if (guard && guard->generation_ == generation) {
                                       guard->reception_ = status;
                                       guard->status_->setText(guard->text("reception." + status.state));
                                   }
                               },
                               [input](const IqBlockView &block) { input->offer(block); }});
            controls(false);
            table_->setRowCount(0);
        } catch (const ReceptionError &error) {
            ++generation_;
            reception_ = error.status;
            reception_error_ = error.what();
            if (input_)
                input_->stop();
            input_.reset();
            status_->setText(text("unavailable"));
        } catch (const std::exception &error) {
            ++generation_;
            reception_error_ = error.what();
            if (input_)
                input_->stop();
            input_.reset();
            status_->setText(text("unavailable"));
        }
    }
    Json diagnostic() const {
        const Json reception = {{"state", reception_.state},           {"detail", reception_.detail},
                                {"requested", reception_.requested},   {"applied", reception_.applied},
                                {"diagnostic", reception_.diagnostic}, {"generation", reception_.generation}};
        if (recording_) {
            Json value = {{"state", recording_->active.load() ? "recording" : "recording_complete"},
                          {"metadata", path_to_utf8(recording_->metadata)},
                          {"event", recording_->event}};
            if (const auto error = recording_->error.load()) {
                value["error"] = *error;
                value["state"] = "recording_failed";
            }
            if (const auto result = recording_->result.load())
                value["decoded_spots"] = result->result.spots.size();
            return value;
        }
        if (!input_)
            return {{"state", "stopped"}, {"reception", reception}, {"error", reception_error_}};
        const auto state = input_->snapshot();
        return {{"reception", reception},
                {"processed_pairs", state.processed_pairs},
                {"input_rejected_pairs", state.input_rejected_pairs},
                {"discontinuities", state.discontinuities},
                {"completed_windows", state.completed_windows},
                {"decoded_spots_total", state.decoded_spots_total},
                {"expired_spots", state.expired_spots},
                {"rejected_windows", state.rejected_windows},
                {"queued_bytes", state.queued_bytes},
                {"queued_memory_bytes", state.queued_memory_bytes},
                {"queued_input_seconds", state.queued_input_seconds},
                {"input_budget_seconds", wspr::LiveInput::input_budget_seconds},
                {"input_budget_bytes", wspr::LiveInput::input_budget_bytes},
                {"input_in_flight_bytes", state.input_in_flight_bytes},
                {"input_in_flight_seconds", state.input_in_flight_seconds},
                {"window_queue_bytes", state.window_queue_bytes},
                {"window_budget_bytes", wspr::LiveInput::window_budget_bytes},
                {"window_in_flight_bytes", state.window_in_flight_bytes},
                {"retained_samples", state.retained_samples},
                {"error", state.error}};
    }
    void refresh() {
        wspr::LiveSnapshot live;
        std::shared_ptr<const wspr::RecordingResult> result;
        const wspr::LiveSnapshot *current = nullptr;
        if (input_) {
            live = input_->snapshot();
            current = &live;
        } else if (recording_) {
            if (const auto error = recording_->error.load()) {
                status_->setText(text("recording_failed"));
                controls(true);
                return;
            }
            result = recording_->result.load();
            if (!result)
                return;
            current = &result->result;
            status_->setText(text("recording_complete"));
            controls(true);
        } else
            return;
        const auto &state = *current;
        metrics_->setText(text(recording_ ? "recording_metrics" : "metrics",
                               {{"seconds", state.retained_samples / 375.},
                                {"windows", state.completed_windows},
                                {"rejected_pairs", state.input_rejected_pairs},
                                {"rejected_windows", state.rejected_windows},
                                {"gaps", state.discontinuities},
                                {"spots", state.spots.size()},
                                {"total_spots", state.decoded_spots_total},
                                {"expired_spots", state.expired_spots}}));
        if (!state.error.empty())
            status_->setText(text("processing_error"));
        const RowStamp stamp{state.spots.size(), state.spots.empty() ? 0 : state.spots.back().source_sample,
                             state.spots.empty() ? 0 : state.spots.back().observation.decoded.message.payload,
                             state.spots.empty() ? 0 : state.spots.back().frequency_hz,
                             state.spots.empty() ? 0 : state.spots.back().epoch};
        if (render_stamp_ && *render_stamp_ == stamp)
            return;
        render_stamp_ = stamp;
        table_->setRowCount(int(state.spots.size()));
        for (std::size_t row = 0; row < state.spots.size(); ++row) {
            const auto &spot = state.spots[row];
            const auto &message = spot.observation.decoded.message;
            const auto call = message.callsign.empty()
                                  ? text("unknown_hash", {{"hash", message.callsign_hash.value_or(0)}})
                                  : QString::fromStdString(message.callsign);
            const QStringList values{call,
                                     message.locator.empty() ? text("unknown_grid")
                                                             : QString::fromStdString(message.locator),
                                     QString::number(message.power_dbm),
                                     QString::number(spot.frequency_hz / 1e6, 'f', 6),
                                     QString::number(spot.observation.drift_hz, 'f', 1),
                                     QString::number(spot.observation.decoded.synchronization, 'f', 3),
                                     QString::number(spot.source_sample)};
            for (int column = 0; column < values.size(); ++column)
                table_->setItem(int(row), column, new QTableWidgetItem(values[column]));
            table_->item(int(row), 6)
                ->setToolTip(text("source_context", {{"epoch", spot.epoch}, {"rate", spot.source_rate}}));
        }
    }
    QString language_;
    TextCatalog catalog_;
    QDoubleSpinBox *frequency_, *minimum_, *maximum_;
    QSpinBox *drift_;
    QLabel *status_, *metrics_;
    QTableWidget *table_;
    QTimer timer_;
    ReceptionAcquire acquire_;
    std::shared_ptr<ReceptionLease> lease_;
    std::shared_ptr<wspr::LiveInput> input_;
    std::uint64_t generation_ = 0;
    struct RecordingState {
        std::atomic<bool> active = false;
        std::atomic<std::shared_ptr<const wspr::RecordingResult>> result;
        std::atomic<std::shared_ptr<const std::string>> error;
        std::filesystem::path metadata;
        Json event = Json::object();
    };
    std::shared_ptr<RecordingState> recording_;
    std::jthread recording_worker_;
    using RowStamp = std::tuple<std::size_t, std::uint64_t, std::uint64_t, double, std::uint64_t>;
    std::optional<RowStamp> render_stamp_;
    ReceptionStatus reception_;
    std::string reception_error_;
};
} // namespace
LiveResearchProvider wsprLiveProvider() {
    return {"wspr-live",
            [](const QString &language, QWidget *parent) { return (new Panel(language, parent))->tool(); }};
}
} // namespace signalscope::desktop
