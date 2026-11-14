#pragma once
#include "plugins/desktop/research/live_provider.hpp"
#include "signalscope/backend/iq/signal_capture.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <QTimer>
#include <atomic>
#include <chrono>
class QLineEdit;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
namespace signalscope::desktop {
class WaveformCaptureControls final:public QWidget {
public:
    WaveformCaptureControls(const QString& language,QWidget* parent=nullptr);
    ~WaveformCaptureControls() override;
    void attachReception(ReceptionAcquire);
    void attachReceiverControls(std::function<void()>);
    void sourceChanged(const LiveResearchContext&);
    void stop();
    void pauseFollowing();
    bool busy() const;
    Json snapshot() const;
    QString health() const;
    QString summary() const;
    std::function<bool()> canAnalyze;
    std::function<void(std::shared_ptr<const SignalWindow>,bool following)> windowReady;
private:
    QString text(const std::string&,const Json& arguments=Json::object()) const;
    std::optional<double> frequency() const;
    void start();
    void received(const ReceptionStatus&,const std::shared_ptr<std::atomic<std::uint64_t>>&);
    void requestWindow(bool following,bool finish=false);
    void finishCapture();
    void poll();
    void refresh();
    QString language_,error_;
    TextCatalog catalog_;
    ReceptionAcquire acquire_;
    std::function<void()> receiver_controls_;
    std::shared_ptr<ReceptionLease> lease_;
    std::shared_ptr<SignalCapture> capture_=std::make_shared<SignalCapture>();
    std::optional<IqStreamParameters> monitor_;
    std::optional<IqStreamParameters> input_stream_;
    std::uint64_t receiver_generation_=UINT64_MAX,pending_=0;
    bool pending_following_=false,finish_after_snapshot_=false;
    Json receiver_=Json::object();
    std::chrono::steady_clock::time_point last_snapshot_{};
    QTimer timer_;
    QLineEdit* frequency_=nullptr;
    QDoubleSpinBox* bandwidth_=nullptr;
    QComboBox* policy_=nullptr;
    QPushButton *start_=nullptr,*stop_=nullptr,*freeze_=nullptr,*receiver_button_=nullptr,*center_=nullptr;
    QCheckBox* follow_=nullptr;
    QLabel *state_=nullptr,*counts_=nullptr,*reason_=nullptr,*conditions_=nullptr;
    QPlainTextEdit* details_=nullptr;
};
}
