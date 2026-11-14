#include "fm_live.hpp"
#include "plugins/desktop/research/builtin/fm_playback.hpp"
#include "plugins/desktop/research/broadcast_presets.hpp"
#include "plugins/desktop/research/broadcast_search.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/decimal_input.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace signalscope::desktop {
namespace {
class FmLivePanel final:public QWidget {
public:
    FmLivePanel(const QString& language,QWidget* parent):QWidget(parent),language_(language),catalog_(TextCatalog::resource("fm-live")) {
        setLocale(QLocale(language));setObjectName("fm-live-panel");setWindowTitle(text("title"));setMinimumWidth(250);
        settings_.beginGroup("live-research/fm-audio");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);
        state_=label();auto font=state_->font();font.setBold(true);state_->setFont(font);layout->addWidget(state_);
        layout->addWidget(label(text("profile")));
        presets_=new BroadcastPresets(settings_,[this](const auto& key){return text(key);},"fm-live",this);layout->addWidget(presets_);
        presets_->current=[this]{const auto value=frequency();return value?Json{{"frequency_hz",*value}}:Json();};
        presets_->sameStation=[](const auto& a,const auto& b){return a.value("frequency_hz",Json())==b.value("frequency_hz",Json());};
        presets_->defaultName=[this](const auto& value){return text("station_name",{{"frequency",number(value.at("frequency_hz").template get<double>()/1e6,3)}});};
        presets_->selected=[this](const auto& value){if(value.value("frequency_hz",Json()).is_number())frequency_->setText(displayDecimal(value.at("frequency_hz").template get<double>()/1e6,6,locale()));};
        auto* frequencyForm=new QFormLayout;frequencyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);frequency_=new QLineEdit;frequency_->setObjectName("fm-live-frequency");
        frequency_->setPlaceholderText(text("frequency_placeholder"));
        frequency_->setValidator(new QRegularExpressionValidator(decimalInputPattern(locale(),7,6),frequency_));
        if(settings_.contains("frequency_hz"))frequency_->setText(displayDecimal(settings_.value("frequency_hz").toDouble()/1e6,6,locale()));
        auto* frequencyRow=new QWidget;auto* frequencyLayout=new QHBoxLayout(frequencyRow);frequencyLayout->setContentsMargins(0,0,0,0);
        center_=new QToolButton;center_->setObjectName("fm-live-use-center");center_->setText(text("use_center"));center_->setToolTip(text("use_center_hint"));
        frequencyLayout->addWidget(frequency_,1);frequencyLayout->addWidget(center_);frequencyForm->addRow(text("frequency"),frequencyRow);layout->addLayout(frequencyForm);
        auto* buttons=new QHBoxLayout;start_=new QPushButton(text("start"));start_->setObjectName("fm-live-start");
        stop_=new QPushButton(text("stop"));stop_->setObjectName("fm-live-stop");buttons->addWidget(start_);buttons->addWidget(stop_);layout->addLayout(buttons);
        auto* playbackControls=new QFormLayout;playbackControls->setRowWrapPolicy(QFormLayout::WrapLongRows);layout->addLayout(playbackControls);
        validation_=label();validation_->setObjectName("fm-live-validation");layout->addWidget(validation_);
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);layout->addWidget(scroll,1);
        auto* body=new QWidget;scroll->setWidget(body);auto* bodyLayout=new QVBoxLayout(body);bodyLayout->setContentsMargins(0,0,0,0);
        auto* scanToggle=new QToolButton;scanToggle->setObjectName("fm-live-scan-toggle");scanToggle->setText(text("scan.title"));scanToggle->setCheckable(true);bodyLayout->addWidget(scanToggle);
        search_=new BroadcastSearch([this](const auto& key,const auto& args){return text(key,args);},"fm-live",87.5,108.,.1,.8,this);bodyLayout->addWidget(search_);search_->setEnabled(false);search_->hide();connect(scanToggle,&QToolButton::toggled,search_,&QWidget::setVisible);
        search_->startProbe=[this](double target){
            lease_.reset();audio_->stop();scanFrequency_=target;scanRate_=0;
            try{scanLease_=acquire_({target,2*fm_channel_guard_hz,fm_minimum_input_rate,fm_maximum_input_rate,{}},{[this](const auto& status){
                if(status.state=="receiving" && status.stream)try{
                    scanRate_=status.stream->sample_rate;probeAudio_->start({scanRate_,status.stream->format,scanFrequency_-status.stream->center_frequency_hz,50,75000},fm_live_maximum_block_bytes);search_->ready();
                }catch(const std::exception& error){search_->fail(text("scan.error",{{"detail",error.what()}}));}
                else if(status.state!="starting")search_->fail(text("error.reception_"+status.state));
            },[probe=probeAudio_](const auto& block){const auto stride=bytes_per_pair(block.stream.format),limit=fm_live_maximum_block_bytes/stride*stride;for(std::size_t at=0;at<block.bytes.size();at+=limit)probe->offer(block.bytes.subspan(at,std::min(limit,block.bytes.size()-at)),block.first_sample+at/stride);}});
            }catch(const std::exception&){search_->fail(text("error.reception_error"));}
        };
        search_->stopProbe=[this]{scanLease_.reset();probeAudio_->stop();};
        search_->candidates=[this]{const auto status=probeAudio_->status();if(status.processed_pairs<scanRate_/5 || !status.signal.snr_db || *status.signal.snr_db<12)return std::vector<Json>{};return std::vector<Json>{{{"frequency_hz",scanFrequency_}}};};
        search_->caption=[this](const auto& value){return presets_->defaultName(value);};
        search_->selected=[this](const auto& value){frequency_->setText(displayDecimal(value.at("frequency_hz").template get<double>()/1e6,6,locale()));};
        source_=label();source_->setObjectName("fm-live-source");bodyLayout->addWidget(source_);
        auto* controls=new QFormLayout;controls->setRowWrapPolicy(QFormLayout::WrapLongRows);bodyLayout->addLayout(controls);
        receiverPolicy_=new QComboBox;receiverPolicy_->setObjectName("fm-live-receiver-policy");receiverPolicy_->addItem(text("reception.auto"),false);receiverPolicy_->addItem(text("reception.configured"),true);receiverPolicy_->setCurrentIndex(settings_.value("configured_receiver",false).toBool()?1:0);controls->addRow(text("reception.policy"),receiverPolicy_);
        receiverSettings_=new QPushButton(text("reception.select_receiver"));receiverSettings_->setObjectName("fm-live-receiver-settings");receiverSettings_->setEnabled(false);controls->addRow(receiverSettings_);connect(receiverSettings_,&QPushButton::clicked,this,[this]{if(receiverControls_)receiverControls_();});
        connect(receiverPolicy_,&QComboBox::currentIndexChanged,this,[this]{settings_.setValue("configured_receiver",receiverPolicy_->currentData());});
        auto* receiverDetailsToggle=new QToolButton;receiverDetailsToggle->setText(text("reception.details"));receiverDetailsToggle->setCheckable(true);controls->addRow(receiverDetailsToggle);
        receiverDetails_=new QPlainTextEdit;receiverDetails_->setObjectName("fm-live-receiver-details");receiverDetails_->setReadOnly(true);receiverDetails_->setMaximumHeight(180);receiverDetails_->hide();controls->addRow(receiverDetails_);connect(receiverDetailsToggle,&QToolButton::toggled,receiverDetails_,&QWidget::setVisible);
        auto* deviceRow=new QWidget;auto* deviceLayout=new QHBoxLayout(deviceRow);deviceLayout->setContentsMargins(0,0,0,0);
        outputs_=new QComboBox;outputs_->setObjectName("fm-live-output");outputs_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);outputs_->setMinimumContentsLength(10);
        auto* refreshButton=new QPushButton(text("device_refresh"));deviceLayout->addWidget(outputs_,1);deviceLayout->addWidget(refreshButton);controls->addRow(text("device_label"),deviceRow);
        selectedDevice_=settings_.value("output_device").toString();refreshOutputs();
        auto* volumeRow=new QWidget;auto* volumeLayout=new QHBoxLayout(volumeRow);volumeLayout->setContentsMargins(0,0,0,0);
        volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("fm-live-volume");volume_->setRange(0,100);volume_->setValue(settings_.value("volume",50).toInt());
        volumeValue_=new QLabel;volumeLayout->addWidget(volume_,1);volumeLayout->addWidget(volumeValue_);playbackControls->addRow(text("volume"),volumeRow);
        mute_=new QCheckBox(text("mute"));mute_->setObjectName("fm-live-mute");mute_->setChecked(settings_.value("muted",false).toBool());playbackControls->addRow(mute_);
        auto* squelchRow=new QWidget;auto* squelchLayout=new QHBoxLayout(squelchRow);squelchLayout->setContentsMargins(0,0,0,0);
        squelch_=new QCheckBox(text("squelch"));squelch_->setObjectName("fm-live-squelch");squelch_->setChecked(settings_.value("squelch",false).toBool());
        threshold_=new QDoubleSpinBox;threshold_->setObjectName("fm-live-squelch-threshold");threshold_->setRange(-20,60);threshold_->setDecimals(0);threshold_->setSuffix(" dB");threshold_->setValue(settings_.value("squelch_db",6).toDouble());
        squelchLayout->addWidget(squelch_);squelchLayout->addWidget(threshold_);controls->addRow(text("squelch_threshold"),squelchRow);squelchRow->setToolTip(text("snr_hint"));
        auto* advancedToggle=new QToolButton;advancedToggle->setObjectName("fm-live-advanced");advancedToggle->setText(text("advanced"));advancedToggle->setCheckable(true);advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);advancedToggle->setArrowType(Qt::RightArrow);bodyLayout->addWidget(advancedToggle);
        auto* advanced=new QWidget;auto* advancedForm=new QFormLayout(advanced);advancedForm->setContentsMargins(0,0,0,0);
        deemphasis_=new QComboBox;deemphasis_->setObjectName("fm-live-deemphasis");
        for(const auto value:{50,75,0})deemphasis_->addItem(value?text("microseconds",{{"value",value}}):text("off"),value);
        deviation_=new QComboBox;deviation_->setObjectName("fm-live-deviation");
        for(const auto value:{75000,50000})deviation_->addItem(text("kilohertz",{{"value",value/1000}}),value);
        deemphasis_->setCurrentIndex(std::max(0,deemphasis_->findData(settings_.value("deemphasis_us",50))));
        deviation_->setCurrentIndex(std::max(0,deviation_->findData(settings_.value("deviation_hz",75000))));
        advancedForm->addRow(text("deemphasis"),deemphasis_);advancedForm->addRow(text("deviation"),deviation_);
        auto* recommended=new QPushButton(text("recommend"));recommended->setObjectName("fm-live-recommend");advancedForm->addRow(recommended);bodyLayout->addWidget(advanced);advanced->hide();
        connect(advancedToggle,&QToolButton::toggled,this,[advancedToggle,advanced](bool visible){advanced->setVisible(visible);advancedToggle->setArrowType(visible?Qt::DownArrow:Qt::RightArrow);});
        auto* quality=new QGroupBox(text("quality"));auto* qualityForm=new QFormLayout(quality);qualityForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        rf_=label();rf_->setObjectName("fm-live-rf-level");snr_=label();snr_->setObjectName("fm-live-snr");snr_->setToolTip(text("snr_hint"));
        pcm_=label();latency_=label();counts_=label();counts_->setObjectName("fm-live-counts");queue_=label();
        qualityForm->addRow(text("level"),rf_);qualityForm->addRow(text("snr"),snr_);qualityForm->addRow(text("output_level"),pcm_);qualityForm->addRow(text("latency_label"),latency_);
        qualityForm->addRow(counts_);qualityForm->addRow(queue_);bodyLayout->addWidget(quality);
        errorBox_=new QGroupBox(text("error_heading"));auto* errorLayout=new QVBoxLayout(errorBox_);error_=label();error_->setObjectName("fm-live-error");errorLayout->addWidget(error_);
        auto* details=new QToolButton;details->setText(text("error_details"));details->setCheckable(true);errorLayout->addWidget(details);
        diagnostic_=new QPlainTextEdit;diagnostic_->setReadOnly(true);diagnostic_->setMaximumHeight(100);diagnostic_->hide();errorLayout->addWidget(diagnostic_);
        connect(details,&QToolButton::toggled,diagnostic_,&QWidget::setVisible);bodyLayout->addWidget(errorBox_);bodyLayout->addStretch();
        connect(start_,&QPushButton::clicked,this,[this]{listen();});connect(stop_,&QPushButton::clicked,this,[this]{stop();});
        connect(center_,&QToolButton::clicked,this,[this]{if(context_.stream)frequency_->clear();});
        connect(refreshButton,&QPushButton::clicked,this,[this]{refreshOutputs();refresh();});
        connect(frequency_,&QLineEdit::textChanged,this,[this]{presets_->clearSelection();refresh();});connect(frequency_,&QLineEdit::returnPressed,this,[this]{if(start_->isEnabled())listen();});
        connect(outputs_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{selectedDevice_=outputs_->currentData().toString();settings_.setValue("output_device",selectedDevice_);try{audio_->setOutputDevice(selectedDevice_.toStdString());}catch(const std::exception& e){externalError_="configuration";externalDetail_=e.what();}refresh();});
        connect(volume_,&QSlider::valueChanged,this,[this](int value){audio_->setVolume(value/100.);settings_.setValue("volume",value);refresh();});
        connect(mute_,&QCheckBox::toggled,this,[this](bool value){audio_->setMuted(value);settings_.setValue("muted",value);refresh();});
        const auto gateChanged=[this]{audio_->setSquelch(squelch_->isChecked()?std::optional<double>(threshold_->value()):std::nullopt);threshold_->setEnabled(squelch_->isChecked());settings_.setValue("squelch",squelch_->isChecked());settings_.setValue("squelch_db",threshold_->value());refresh();};
        connect(squelch_,&QCheckBox::toggled,this,gateChanged);connect(threshold_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,gateChanged);
        connect(deemphasis_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refresh();});connect(deviation_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refresh();});
        connect(recommended,&QPushButton::clicked,this,[this]{deemphasis_->setCurrentIndex(deemphasis_->findData(50));deviation_->setCurrentIndex(deviation_->findData(75000));refresh();});
        audio_->setVolume(volume_->value()/100.);audio_->setMuted(mute_->isChecked());gateChanged();
        timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,[this]{if(++ticks_%20==0)refreshOutputs();refresh();});timer_.start();refresh();
    }
    LiveResearchTool tool() {
        auto result=LiveResearchTool{this,[this](const auto& context){if(!lease_){context_=context;if(!context.receiving && !acquire_)audio_->stop();}refresh();},
            [playback=audio_](const auto& block){
                const auto stride=bytes_per_pair(block.stream.format),limit=fm_live_maximum_block_bytes/stride*stride;
                for(std::size_t at=0;at<block.bytes.size();at+=limit)playback->offer(block.bytes.subspan(at,std::min(limit,block.bytes.size()-at)),block.first_sample+at/stride);
            },
            [this](const auto& event){return actions(event);},[this]{stop();},[this]{return busy();},
            [this](std::exception_ptr failure){stop();externalError_="consumer_failed";try{std::rethrow_exception(failure);}catch(const std::exception& e){externalDetail_=e.what();}catch(...){externalDetail_="unknown_exception";}refresh();},false,[this]{return snapshot();}};
        offer_=result.offer;
        result.attachReception=[this](ReceptionAcquire acquire){acquire_=std::move(acquire);search_->setEnabled(bool(acquire_));if(frequency_->text().isEmpty())frequency_->setText(displayDecimal(98.,6,locale()));refresh();};
        result.attachReceiverControls=[this](auto action){receiverControls_=std::move(action);receiverSettings_->setEnabled(bool(receiverControls_));};
        result.summary=[this]{return search_->active()?search_->summary():busy()?state_->text():QString();};result.health=[this]{const auto status=audio_->status();return !externalError_.empty()?text("error."+externalError_):!status.error_key.empty()?text("error."+status.error_key):QString();};return result;
    }
private:
    Json snapshot() const {
        const auto status=audio_->status();const auto& input=status.input;
        Json result={{"state",status.state},{"frequency_hz",appliedFrequency_},{"device",status.device_name},
            {"error_key",externalError_.empty()?status.error_key:externalError_},{"error_detail",externalError_.empty()?status.error_detail:externalDetail_},
            {"volume",status.volume},{"muted",status.muted},{"squelched",status.squelched},{"underruns",status.underruns},
            {"written_frames",status.written_frames},{"input_pairs",input.input_pairs},{"processed_pairs",input.processed_pairs},
            {"dropped_pairs",input.dropped_pairs},{"discontinuities",input.discontinuities},{"pcm_discarded_frames",input.pcm_discarded_frames},
            {"queue_bytes",input.queue_bytes},{"queue_high_water_bytes",input.queue_high_water_bytes},{"queue_capacity_bytes",input.queue_capacity_bytes},
            {"queued_seconds",status.queued_seconds}};
        if(input.signal.snr_db)result["snr_db"]=*input.signal.snr_db;
        if(input.signal.power_dbfs)result["power_dbfs"]=*input.signal.power_dbfs;
        result["reception"]={{"state",reception_.state},{"detail",reception_.detail},{"requested",reception_.requested},{"applied",reception_.applied},{"generation",reception_.generation},{"diagnostic",reception_.diagnostic}};
        return result;
    }
    QString text(const std::string& key,const Json& args=Json::object()) const {
        Json node={{"label_key",key},{"arguments",args}};catalog_.resolve(node);return localizedText(node.at("label"),language_,QString::fromStdString(key));
    }
    QLabel* label(const QString& value={}) {auto* result=new QLabel(value);result->setWordWrap(true);result->setTextFormat(Qt::PlainText);return result;}
    std::string number(double value,int decimals=1) const {return displayDecimal(value,decimals,locale()).toStdString();}
    std::optional<double> frequency() const {
        if(frequency_->text().isEmpty())return context_.stream?std::optional<double>(context_.stream->center_frequency_hz):std::nullopt;
        bool valid=false;const auto value=canonicalDecimalInput(frequency_->text(),locale()).toDouble(&valid)*1e6;
        if(!valid || !frequency_->hasAcceptableInput() || !std::isfinite(value))return std::nullopt;return std::round(value);
    }
    FmAudioConfig configuration(double frequency) const {return {context_.stream->sample_rate,context_.stream->format,frequency-context_.stream->center_frequency_hz,deemphasis_->currentData().toDouble(),deviation_->currentData().toDouble()};}
    Json bounds() const {
        if(!context_.stream)return Json::object();const auto& stream=*context_.stream;
        return {{"low",number((stream.center_frequency_hz-stream.sample_rate/2.+fm_channel_guard_hz)/1e6,3)},
            {"high",number((stream.center_frequency_hz+stream.sample_rate/2.-fm_channel_guard_hz)/1e6,3)},{"rate",number(stream.sample_rate/1e6,3)}};
    }
    QString issue(std::optional<double> proposed={}) const {
        const auto target=proposed?proposed:frequency();if(!target)return text("frequency_invalid");
        if(!acquire_ && !context_.receiving)return text("source_idle");if(!acquire_ && !context_.stream)return text("source_wait");
        if(*target<0 || *target>=std::ldexp(1.,64))return text("frequency_invalid");
        const auto config=acquire_?FmAudioConfig{1000000,SampleFormat::ci16_le,0,deemphasis_->currentData().toDouble(),deviation_->currentData().toDouble()}:configuration(*target);
        switch(fmAudioConfigurationIssue(config)) {
        case FmAudioIssue::input_rate:return text("input_rate_invalid",{{"low",number(fm_minimum_input_rate/1e6,6)},{"high",number(fm_maximum_input_rate/1e6,0)}});
        case FmAudioIssue::frequency:return text("frequency_outside",bounds());
        case FmAudioIssue::deemphasis:return text("deemphasis_invalid");case FmAudioIssue::deviation:return text("deviation_invalid");case FmAudioIssue::none:break;
        }
        if(!FmPlayback::supported())return text("error.audio_unavailable");
        for(const auto& device:devices_)if((selectedDevice_.isEmpty() && device.system_default) || device.id==selectedDevice_.toStdString())return device.supports_pcm?QString():text("error.output_format");
        return text("error.output_unavailable");
    }
    bool busy() const {const auto state=audio_->status().state;return lease_ || (search_ && search_->active()) || (state!="stopped" && state!="failed");}
    void refreshOutputs() {
        const auto devices=FmPlayback::outputDevices();if(devices==devices_ && outputs_->count())return;devices_=devices;
        const QSignalBlocker blocker(outputs_);outputs_->clear();
        QString defaultName=text("device_none");for(const auto& device:devices_)if(device.system_default)defaultName=QString::fromUtf8(device.name.c_str());
        outputs_->addItem(text("device_default",{{"device",defaultName.toStdString()}}),QString());
        for(const auto& device:devices_)outputs_->addItem(QString::fromUtf8(device.name.c_str()),QString::fromStdString(device.id));
        auto index=outputs_->findData(selectedDevice_);if(index<0){outputs_->addItem(text("device_missing"),selectedDevice_);index=outputs_->count()-1;}outputs_->setCurrentIndex(index);
    }
    void listen() {
        if(!issue().isEmpty())return;const auto target=*frequency();externalError_.clear();externalDetail_.clear();
        if(acquire_) {
            stop();wantedFrequency_=target;reception_={};
            try{lease_=acquire_({target,2*fm_channel_guard_hz,fm_minimum_input_rate,fm_maximum_input_rate,{},receiverPolicy_->currentData().toBool()},{[this](const auto& status){
                reception_=status;
                if(status.state=="receiving" && status.stream){context_={true,status.stream};startAudio(wantedFrequency_);}
                else if(status.state=="starting")audio_->stop();
                else {audio_->stop();lease_.reset();externalError_="reception_"+status.state;externalDetail_=status.detail;}
                refresh();
            },offer_});}catch(const std::exception& e){externalError_="configuration";externalDetail_=e.what();}refresh();return;
        }
        startAudio(target);
    }
    void startAudio(double target) {
        externalError_.clear();externalDetail_.clear();
        try {const auto stride=bytes_per_pair(context_.stream->format);audio_->start(configuration(target),fm_live_maximum_block_bytes/stride*stride,selectedDevice_.toStdString());appliedFrequency_=target;
            if(frequency_->text().isEmpty())settings_.remove("frequency_hz");else settings_.setValue("frequency_hz",target);
            settings_.setValue("deemphasis_us",deemphasis_->currentData());settings_.setValue("deviation_hz",deviation_->currentData());
        }catch(const FmPlaybackError& e){lease_.reset();externalError_=e.key;externalDetail_=e.what();}
        catch(const std::exception& e){lease_.reset();externalError_="configuration";externalDetail_=e.what();}refresh();
    }
    void stop(){if(search_)search_->cancel();lease_.reset();audio_->stop();reception_.state="stopped";refresh();}
    void refresh() {
        if(!start_)return;const auto status=audio_->status();const bool running=status.state!="stopped" && status.state!="failed";
        if(status.state=="failed")lease_.reset();
        presets_->setSaveAvailable(frequency().has_value());
        const auto validation=issue();start_->setEnabled(validation.isEmpty());start_->setToolTip(validation);start_->setText(text(busy()?"retune":"start"));stop_->setEnabled(busy());
        validation_->setText(validation);validation_->setVisible(!validation.isEmpty());center_->setEnabled(context_.stream.has_value());
        const bool validRate=context_.stream && context_.stream->sample_rate>=fm_minimum_input_rate && context_.stream->sample_rate<=fm_maximum_input_rate;
        source_->setText(validRate?text("source_range",bounds()):QString());
        std::string state="state_"+status.state;if(status.state=="playing")state=status.muted?"state_muted":status.squelched?"state_squelched":"state_playing";
        state_->setText(text(state,{{"frequency",number(appliedFrequency_/1e6,6)}}));volumeValue_->setText(text("volume_value",{{"percent",volume_->value()}}));
        if(lease_ && reception_.state=="starting")state_->setText(text("reception_starting"));
        if(lease_)source_->setToolTip(QString::fromUtf8(Json{{"requested",reception_.requested},{"applied",reception_.applied}}.dump(2)));
        if(receiverDetails_->isVisible())receiverDetails_->setPlainText(QString::fromUtf8(Json{{"requested",reception_.requested},{"applied",reception_.applied}}.dump(2)));
        rf_->setText(status.input.signal.power_dbfs?text("dbfs",{{"value",number(*status.input.signal.power_dbfs)}}):text("not_available"));
        snr_->setText(status.input.signal.snr_db?text("db",{{"value",number(*status.input.signal.snr_db)}}):text("not_available"));pcm_->setText(text("dbfs",{{"value",number(status.output_level_dbfs)}}));
        latency_->setText(text("latency",{{"ms",number(status.queued_seconds*1000,0)}}));
        counts_->setText(text("counts",{{"pairs",status.input.dropped_pairs},{"underruns",status.underruns},{"frames",status.input.pcm_discarded_frames}}));
        queue_->setText(text("queue_detail",{{"peak",number(status.input.queue_high_water_bytes/1048576.,2)},{"capacity",number(status.input.queue_capacity_bytes/1048576.,2)},{"gaps",status.input.discontinuities}}));
        const auto key=externalError_.empty()?status.error_key:externalError_;const auto detail=externalError_.empty()?status.error_detail:externalDetail_;
        errorBox_->setVisible(!key.empty());if(!key.empty()){error_->setText(text("error."+key));diagnostic_->setPlainText(text("diagnostic",{{"code",key},{"detail",detail}}));}
    }
    std::vector<LiveResearchAction> actions(const Json& event) {
        const auto level=event.find("evidence_level");
        if(level==event.end() || !level->is_string() || *level!="broadcast_fm_stereo" || !event.contains("center_frequency_hz") || !event.at("center_frequency_hz").is_number())return {};
        const auto frequency=std::round(event.at("center_frequency_hz").get<double>());if(!std::isfinite(frequency) || frequency<0 || frequency>=std::ldexp(1.,64))return {};
        const auto select=[this,frequency]{frequency_->setText(displayDecimal(frequency/1e6,6,locale()));};const auto reason=issue(frequency);
        return {{"open",text("event_open"),{},true,select},{"listen",text("event_listen"),reason,reason.isEmpty(),[this,select]{select();listen();}},
            {"favorite",text("event_favorite"),{},true,[this,select,frequency]{select();presets_->saveValue({{"frequency_hz",frequency}});}}};
    }
    QString language_;TextCatalog catalog_;QSettings settings_;LiveResearchContext context_;std::shared_ptr<FmPlayback> audio_=std::make_shared<FmPlayback>();QTimer timer_;unsigned ticks_=0;
    std::vector<AudioOutputDevice> devices_;QString selectedDevice_;double appliedFrequency_=0;std::string externalError_,externalDetail_;
    QLineEdit* frequency_=nullptr;QPushButton *start_=nullptr,*stop_=nullptr;QComboBox *outputs_=nullptr,*deemphasis_=nullptr,*deviation_=nullptr;
    QSlider* volume_=nullptr;QCheckBox *mute_=nullptr,*squelch_=nullptr;QDoubleSpinBox* threshold_=nullptr;
    QLabel *state_=nullptr,*source_=nullptr,*validation_=nullptr,*volumeValue_=nullptr,*rf_=nullptr,*snr_=nullptr,*pcm_=nullptr,*latency_=nullptr,*counts_=nullptr,*queue_=nullptr,*error_=nullptr;
    QGroupBox* errorBox_=nullptr;QPlainTextEdit* diagnostic_=nullptr;
    QToolButton* center_=nullptr;
    BroadcastPresets* presets_=nullptr;ReceptionAcquire acquire_;std::shared_ptr<ReceptionLease> lease_;ReceptionStatus reception_;IqBlockSubscriber offer_;double wantedFrequency_=0;
    BroadcastSearch* search_=nullptr;std::shared_ptr<ReceptionLease> scanLease_;std::shared_ptr<LiveFmAudio> probeAudio_=std::make_shared<LiveFmAudio>();double scanFrequency_=0;std::uint32_t scanRate_=0;
    QComboBox* receiverPolicy_=nullptr;
    QPlainTextEdit* receiverDetails_=nullptr;
    QPushButton* receiverSettings_=nullptr;std::function<void()> receiverControls_;
};
}
LiveResearchProvider fmLiveResearchProvider(){return {"fm-audio",[](const QString& language,QWidget* parent){return (new FmLivePanel(language,parent))->tool();}};}
}
