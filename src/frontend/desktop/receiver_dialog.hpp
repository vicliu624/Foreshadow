#pragma once

#include <QDialog>
#include <QProcess>
#include <QTimer>
#include <cstdint>
#include <optional>
#include <vector>
#include "signalscope/backend/sessions/receiver_configuration.hpp"
#include "signalscope/adapters/modulation/detection_selection.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"

class QComboBox;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QTableWidget;
class QSplitter;
class QResizeEvent;
class QPlainTextEdit;

namespace signalscope::desktop {
class SearchWidthControl;
class ParameterEditor;
struct ReceiverSelection {
    QString backend;
    std::uint32_t index=0;
    QString antenna;
    std::optional<std::vector<int>> manualGains=std::nullopt;
    std::uint32_t sampleRate=0;
    QString serial;
    std::uint64_t frequency=470500000;
    double searchWidth=1000000;
    QString gain;
    LoRaConfig lora;
    bool enableLora=true,narrowband=true,snapshots=false;
    std::optional<ReceiverCapabilities> capabilities;
    Json applied;
    QString searchWidthMode="fixed";
    NarrowbandSelection narrowbandDetectors;
    SnapshotSelection snapshotDetectors;
    QString inputMode="auto";
    double ifBandwidth=0;
    Json settings=Json::object();
};

class ReceiverDialog : public QDialog {
public:
    ReceiverDialog(const ReceiverSelection& current,QWidget* parent,std::function<QString(const ReceiverSelection&)> access={});
    ~ReceiverDialog() override;
    ReceiverSelection selection() const;
    void reject() override;

private:
    enum class Request { devices, probe, apply };
    void updatePlan();
    void apply();
    Config configuration() const;
    void scan();
    void probe();
    void launch(Request request,QStringList arguments);
    void finished(int code,QProcess::ExitStatus status);
    void fail(const QString& detail,const QString& message={});
    void setBusy(bool busy);
    void populateRates(std::uint32_t preferred);
    std::optional<MonitorRecommendation> recommendation() const;
    void useRecommended();
    void clearPlan();
    void populateConnection();
    void resizeEvent(QResizeEvent*) override;

    ReceiverSelection initial_;
    std::function<QString(const ReceiverSelection&)> access_;
    QComboBox *backend_,*device_,*antenna_,*sampleRate_,*inputMode_,*ifBandwidth_;
    QLabel *inputModeLabel_,*ifBandwidthLabel_,*inputProblem_,*ifProblem_;
    QLabel* status_;
    QLabel *feedback_,*planStatus_,*recommendedSummary_;
    QLabel* coverageSummary_;
    QLabel *rateProblem_,*frequencyProblem_,*antennaProblem_,*widthProblem_,*gainProblem_;
    QLabel* gainLabel_;
    QDoubleSpinBox* frequency_;
    SearchWidthControl* searchWidth_;
    ParameterEditor* pluginParameters_;
    ParameterEditor* connectionParameters_;
    QCheckBox* advancedRates_;
    QComboBox* gain_;
    std::optional<ReceiverCapabilities> capabilities_;
    Json applied_;
    bool busy_=false;
    QPushButton *refresh_,*accept_;
    QPushButton *recommended_,*repairWidth_,*repairRate_,*repairAntenna_,*autoGain_,*details_;
    QTableWidget* modulationTable_;
    QLabel* modulationSummary_;
    QPlainTextEdit* modulationDetails_;
    QSplitter* panels_=nullptr;
    QProcess process_;
    QTimer deadline_;
    Request request_=Request::devices;
    bool timed_out_=false;
    std::optional<std::vector<int>> manualGains_;
};
}
