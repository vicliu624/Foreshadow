#include "capture_controls.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/decimal_input.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QUuid>
#include <QVBoxLayout>

namespace signalscope::desktop {
WaveformCaptureControls::WaveformCaptureControls(const QString& language,QWidget* parent):QWidget(parent),language_(language),catalog_(TextCatalog::resource("waveform-laboratory")) {
    setLocale(QLocale(language));setObjectName("waveform-capture-controls");auto* layout=new QVBoxLayout(this);
    const auto label=[&](const QString& value){auto* result=new QLabel(value);result->setTextFormat(Qt::PlainText);result->setWordWrap(true);return result;};
    auto* form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);layout->addLayout(form);
    frequency_=new QLineEdit;frequency_->setObjectName("waveform-capture-frequency");frequency_->setPlaceholderText(text("capture.frequency_hint"));frequency_->setValidator(new QRegularExpressionValidator(decimalInputPattern(locale(),14,6),frequency_));
    if(QSettings().contains("waveform/capture_frequency_hz"))frequency_->setText(displayDecimal(QSettings().value("waveform/capture_frequency_hz").toDouble()/1e6,6,locale()));
    form->addRow(text("capture.frequency"),frequency_);
    center_=new QPushButton(text("capture.use_monitor"));form->addRow(center_);connect(center_,&QPushButton::clicked,this,[this]{if(monitor_)frequency_->setText(displayDecimal(monitor_->center_frequency_hz/1e6,6,locale()));});
    bandwidth_=new QDoubleSpinBox;bandwidth_->setObjectName("waveform-capture-bandwidth");bandwidth_->setRange(.001,100);bandwidth_->setDecimals(3);bandwidth_->setValue(QSettings().value("waveform/capture_bandwidth_mhz",.5).toDouble());form->addRow(text("capture.bandwidth"),bandwidth_);
    policy_=new QComboBox;policy_->setObjectName("waveform-capture-policy");policy_->addItem(text("capture.auto"),false);policy_->addItem(text("capture.configured"),true);policy_->setCurrentIndex(QSettings().value("waveform/capture_configured",false).toBool()?1:0);form->addRow(text("capture.policy"),policy_);
    receiver_button_=new QPushButton(text("capture.receiver"));receiver_button_->setObjectName("waveform-capture-receiver");form->addRow(receiver_button_);connect(receiver_button_,&QPushButton::clicked,this,[this]{if(receiver_controls_)receiver_controls_();});
    auto* actions=new QHBoxLayout;start_=new QPushButton(text("capture.start"));start_->setObjectName("waveform-capture-start");stop_=new QPushButton(text("capture.stop"));stop_->setObjectName("waveform-capture-stop");actions->addWidget(start_);actions->addWidget(stop_);layout->addLayout(actions);
    freeze_=new QPushButton(text("capture.freeze"));freeze_->setObjectName("waveform-capture-freeze");layout->addWidget(freeze_);
    follow_=new QCheckBox(text("capture.follow"));follow_->setObjectName("waveform-capture-follow");layout->addWidget(follow_);
    state_=label(text("capture.idle"));counts_=label({});reason_=label({});reason_->setObjectName("waveform-capture-error");layout->addWidget(state_);layout->addWidget(counts_);layout->addWidget(reason_);
    conditions_=label({});conditions_->setObjectName("waveform-capture-conditions");layout->addWidget(conditions_);
    layout->addWidget(label(text("capture.budget")));layout->addWidget(label(text("capture.help")));
    details_=new QPlainTextEdit;details_->setReadOnly(true);details_->setObjectName("waveform-capture-readback");details_->setMinimumHeight(100);layout->addWidget(details_,1);
    connect(start_,&QPushButton::clicked,this,[this]{start();});connect(stop_,&QPushButton::clicked,this,[this]{finishCapture();});connect(freeze_,&QPushButton::clicked,this,[this]{follow_->setChecked(false);requestWindow(false);});
    connect(frequency_,&QLineEdit::textChanged,this,[this]{refresh();});connect(bandwidth_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{refresh();});
    connect(follow_,&QCheckBox::toggled,this,[this](bool value){if(!value && pending_following_)pending_=0;refresh();});
    timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,[this]{poll();});timer_.start();refresh();
}
WaveformCaptureControls::~WaveformCaptureControls(){stop();capture_->close();}
QString WaveformCaptureControls::text(const std::string& key,const Json& arguments) const {Json value={{"label_key",key},{"arguments",arguments}};catalog_.resolve(value);return localizedText(value.at("label"),language_,QString::fromStdString(key));}
std::optional<double> WaveformCaptureControls::frequency() const {
    bool valid=false;const auto value=canonicalDecimalInput(frequency_->text().trimmed(),locale()).toDouble(&valid)*1e6;
    if(!valid || !frequency_->hasAcceptableInput() || !std::isfinite(value) || value<1 || value>=std::ldexp(1.,64))return {};return value;
}
void WaveformCaptureControls::attachReception(ReceptionAcquire acquire){if(!acquire)stop();acquire_=std::move(acquire);refresh();}
void WaveformCaptureControls::attachReceiverControls(std::function<void()> callback){receiver_controls_=std::move(callback);refresh();}
void WaveformCaptureControls::sourceChanged(const LiveResearchContext& context){monitor_=context.stream;if(monitor_ && frequency_->text().isEmpty())frequency_->setText(displayDecimal(monitor_->center_frequency_hz/1e6,6,locale()));refresh();}
void WaveformCaptureControls::pauseFollowing(){follow_->setChecked(false);if(finish_after_snapshot_)capture_->stop();pending_=0;finish_after_snapshot_=false;}
void WaveformCaptureControls::stop(){lease_.reset();pauseFollowing();capture_->stop();input_stream_.reset();receiver_generation_=UINT64_MAX;refresh();}
bool WaveformCaptureControls::busy() const{return bool(lease_) || capture_->status().active || pending_;}
QString WaveformCaptureControls::health() const{return error_;}
QString WaveformCaptureControls::summary() const{return busy()?text("capture.summary"):QString();}
void WaveformCaptureControls::start() {
    const auto center=frequency();if(!center || !acquire_)return;
    stop();error_.clear();receiver_=Json::object();details_->clear();
    ReceptionDemand demand;demand.frequency_hz=*center;demand.passband_hz=bandwidth_->value()*1e6;demand.minimum_rate=static_cast<std::uint32_t>(std::ceil(demand.passband_hz));demand.maximum_rate=100000000;demand.configured_receiver=policy_->currentData().toBool();
    auto inputGeneration=std::make_shared<std::atomic<std::uint64_t>>(0);const auto capture=capture_;const auto blockLimit=SignalCaptureBudget{}.maximum_block_bytes;
    try {
        lease_=acquire_(demand,{[this,inputGeneration](const auto& status){received(status,inputGeneration);},[capture,inputGeneration,blockLimit](const auto& block){
            const auto generation=inputGeneration->load();if(!generation)return;
            try {const auto stride=bytes_per_pair(block.stream.format);if(block.bytes.empty() || block.bytes.size()%stride || block.bytes.size()/stride>UINT64_MAX-block.first_sample){capture->offer(block,generation);return;}
                const auto limit=blockLimit/stride*stride;for(std::size_t at=0;at<block.bytes.size();at+=limit)capture->offer({block.bytes.subspan(at,std::min(limit,block.bytes.size()-at)),block.first_sample+at/stride,block.stream,at==0 && block.discontinuous},generation);
            }catch(...){capture->offer(block,generation);}
        }});
        if(!lease_)throw std::runtime_error("Reception provider returned no lease");
        if(!error_.isEmpty()){lease_.reset();capture_->stop();return;}
        QSettings().setValue("waveform/capture_frequency_hz",*center);QSettings().setValue("waveform/capture_bandwidth_mhz",bandwidth_->value());QSettings().setValue("waveform/capture_configured",demand.configured_receiver);follow_->setChecked(true);
    }catch(const ReceptionError& failure){received(failure.status,inputGeneration);}
    catch(...){error_=text("capture.error");capture_->stop();lease_.reset();}
    refresh();
}
void WaveformCaptureControls::received(const ReceptionStatus& status,const std::shared_ptr<std::atomic<std::uint64_t>>& token) {
    receiver_={{"protocol_version",receptionProtocolVersion},{"state",status.state},{"detail",status.detail},{"generation",std::to_string(status.generation)},{"requested",status.requested},{"applied",status.applied},{"diagnostic",status.diagnostic},{"acquisition_conditions",status.conditions}};
    details_->setPlainText(QString::fromStdString(receiver_.dump(2)));
    if(status.state=="receiving" && status.stream) {
        const bool changed=!input_stream_ || input_stream_->sample_rate!=status.stream->sample_rate || input_stream_->format!=status.stream->format || input_stream_->center_frequency_hz!=status.stream->center_frequency_hz;
        if(receiver_generation_!=status.generation || !capture_->status().active || changed) {
            pending_=0;receiver_generation_=status.generation;input_stream_=status.stream;
            token->store(capture_->start(*status.stream,"capture/"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),receiver_));
        }
    }else if(status.state=="starting"){token->store(0);capture_->stop();pending_=0;receiver_generation_=UINT64_MAX;}
    else if(status.state=="source_ended" && capture_->status().active){lease_.reset();follow_->setChecked(false);requestWindow(false,true);}
    else {token->store(0);capture_->stop();lease_.reset();follow_->setChecked(false);pending_=0;error_=text("capture.reception_error",{{"state",status.state}});}
    refresh();
}
void WaveformCaptureControls::requestWindow(bool following,bool finish) {
    if(!capture_->status().active)return;
    pending_following_=following;finish_after_snapshot_=finish;pending_=capture_->requestSnapshot();last_snapshot_=std::chrono::steady_clock::now();refresh();
}
void WaveformCaptureControls::finishCapture() {
    lease_.reset();follow_->setChecked(false);
    if(capture_->status().active && capture_->status().processed_samples)requestWindow(false,true);else stop();
}
void WaveformCaptureControls::poll() {
    if(const auto result=capture_->takeSnapshot();result && pending_ && result->request==pending_) {
        pending_=0;const auto finish=finish_after_snapshot_;finish_after_snapshot_=false;
        if(result->window){if(windowReady)windowReady(result->window,pending_following_);}else error_=text("capture.empty");
        if(finish)capture_->stop();
    }
    const auto status=capture_->status();if(!status.error.empty())error_=text("capture.processing_error");
    if(follow_->isChecked() && status.active && status.processed_samples && !pending_ && (!canAnalyze || canAnalyze()) && std::chrono::steady_clock::now()-last_snapshot_>=std::chrono::milliseconds(500))requestWindow(true);
    refresh();
}
void WaveformCaptureControls::refresh() {
    const auto status=capture_->status();const auto active=bool(lease_) || status.active;
    start_->setEnabled(bool(acquire_) && !active && frequency().has_value());stop_->setEnabled(active);freeze_->setEnabled(status.active && status.processed_samples && !pending_);
    frequency_->setEnabled(!active);bandwidth_->setEnabled(!active);policy_->setEnabled(!active);center_->setEnabled(!active && monitor_.has_value());receiver_button_->setEnabled(bool(receiver_controls_));
    state_->setText(pending_?text("capture.freezing"):status.active?follow_->isChecked()?text("capture.following"):text("capture.frozen_receiving"):lease_?text("capture.starting"):text("capture.idle"));
    counts_->setText(text("capture.counts",{{"accepted",status.accepted_samples},{"rejected",status.rejected_samples},{"processing",status.processing_rejected_samples},{"expired",status.window.expired_samples},{"queue",status.queued_payload_bytes},{"retained",status.window.retained_payload_bytes}}));
    reason_->setText(error_);
    const auto conditions=receiver_.value("acquisition_conditions",Json(AcquisitionConditions{}));
    conditions_->setText(text("capture.conditions",{{"frequency",text("condition."+conditions.at("frequency_calibration").at("state").get<std::string>()).toStdString()},
        {"amplitude",text("condition."+conditions.at("amplitude_calibration").at("state").get<std::string>()).toStdString()},
        {"monitor",text("condition."+conditions.at("adc_overload").at("capability").get<std::string>()).toStdString()},
        {"overload",text("condition."+conditions.at("adc_overload").at("state").get<std::string>()).toStdString()}}));
}
Json WaveformCaptureControls::snapshot() const {
    const auto status=capture_->status();return {{"active",status.active},{"following",follow_->isChecked()},{"generation",std::to_string(status.generation)},
        {"accepted_samples",status.accepted_samples},{"processed_samples",status.processed_samples},{"rejected_samples",status.rejected_samples},{"processing_rejected_samples",status.processing_rejected_samples},{"canceled_samples",status.canceled_samples},
        {"retained_bytes",status.window.retained_payload_bytes},{"expired_samples",status.window.expired_samples},{"known_missing_samples",status.window.known_missing_samples},{"unquantified_discontinuities",status.window.unquantified_discontinuities},
        {"queued_bytes",status.queued_payload_bytes},{"peak_queued_bytes",status.peak_queued_payload_bytes},{"in_flight_bytes",status.in_flight_payload_bytes},
        {"malformed_input_blocks",status.malformed_blocks},{"oversized_input_blocks",status.oversized_blocks},{"queue_contention_blocks",status.contention_blocks},{"queue_full_blocks",status.queue_full_blocks},
        {"configuration_rejected_blocks",status.configuration_rejected_blocks},{"inactive_input_blocks",status.inactive_blocks},
        {"stale_generation_samples_total",status.stale_generation_samples_total},{"stale_generation_blocks_total",status.stale_generation_blocks_total},{"receiver",receiver_}};
}
}
