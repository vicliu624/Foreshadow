#include "dtmb_tv.hpp"
#include "plugins/desktop/common/parameter_editor.hpp"
#include "plugins/desktop/research/media_playback.hpp"
#include "plugins/desktop/research/broadcast_presets.hpp"
#include "plugins/desktop/research/broadcast_search.hpp"
#include "signalscope/adapters/television/dtmb_receiver.hpp"
#include "signalscope/backend/transport/broadcast_text.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/decimal_input.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <condition_variable>
#include <thread>

namespace signalscope::desktop {
namespace {
constexpr std::size_t iqBudget=64*1024*1024;
using namespace television;
struct QueuedIq {Bytes bytes;std::uint64_t first;IqStreamParameters input;bool gap;};
struct TvRun {
    std::mutex mutex;std::condition_variable changed;std::deque<QueuedIq> queue;
    std::size_t bytes=0;std::atomic_bool live=false,done=false;
    std::atomic<std::uint64_t> rejected=0,processed=0,received=0,peak=0;
    std::atomic<std::uint64_t> transportRead=0;
    const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    std::atomic<std::int64_t> finishedMs=-1;double sampleRate=0;
    std::atomic_bool gap=false;
    std::shared_ptr<const Json> decoder,diagnostic;
    void offer(const IqBlockView& block) noexcept {
        if(!live)return;
        std::uint64_t pairs=0;
        try {
            pairs=block.bytes.size()/bytes_per_pair(block.stream.format);received+=pairs;
            if(block.bytes.size()>4*1024*1024){rejected+=pairs;gap=true;return;}
            QueuedIq value{Bytes(block.bytes.begin(),block.bytes.end()),block.first_sample,block.stream,block.discontinuous};
            std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock || !live || bytes>iqBudget-value.bytes.size()){rejected+=pairs;gap=true;return;}
            value.gap=value.gap || gap.exchange(false);bytes+=value.bytes.size();peak=std::max<std::uint64_t>(peak.load(),bytes);
            queue.push_back(std::move(value));changed.notify_one();
        }catch(...){rejected+=pairs;gap=true;}
    }
};
struct TvIngress {std::shared_ptr<TvRun> run;};
class TelevisionImage final:public QWidget {
public:
    explicit TelevisionImage(QWidget* parent=nullptr):QWidget(parent){setMinimumSize(240,160);setObjectName("dtmb-tv-video");}
    void showFrame(std::shared_ptr<const MediaVideoFrame> value){if(value==frame_)return;frame_=std::move(value);update();}
    void placeholder(QString value){if(value==placeholder_)return;placeholder_=std::move(value);update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.fillRect(rect(),QColor(5,9,15));
        if(!frame_ || frame_->image.isNull()){painter.setPen(QColor(164,179,193));painter.drawText(rect().adjusted(16,16,-16,-16),Qt::AlignCenter|Qt::TextWordWrap,placeholder_);return;}
        const auto aspect=frame_->aspect_ratio;QSizeF size(width(),width()/aspect);
        if(size.height()>height())size={height()*aspect,double(height())};
        const QRectF target((width()-size.width())/2,(height()-size.height())/2,size.width(),size.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(target,frame_->image);
    }
private:std::shared_ptr<const MediaVideoFrame> frame_;QString placeholder_;
};
class DtmbTvPanel final:public QWidget {
public:
    DtmbTvPanel(QString language,QWidget* parent,std::filesystem::path helper):QWidget(parent),language_(std::move(language)),catalog_(TextCatalog::resource("dtmb-tv")),mediaHelper_(std::move(helper)) {
        setObjectName("dtmb-tv-panel");setWindowTitle(text("title"));setLocale(QLocale(language_));setMinimumSize(420,300);
        settings_.beginGroup("live-research/dtmb-tv");
        pending_=dtmbRecommendedParameters();pending_["text_encoding"]="GB18030";
        const auto saved=Json::parse(settings_.value("parameters").toString().toUtf8().toStdString(),nullptr,false);
        if(saved.is_object())pending_.update(saved);
        auto* outer=new QVBoxLayout(this);outer->setContentsMargins(8,8,8,8);
        presets_=new BroadcastPresets(settings_,[this](const auto& key){return text(key);},"dtmb-tv",this);outer->addWidget(presets_);
        presets_->current=[this]{return station();};
        presets_->sameStation=[](const Json& a,const Json& b){
            if(a.value("frequency_hz",Json())!=b.value("frequency_hz",Json()) || a.value("program_id",Json())!=b.value("program_id",Json()))return false;
            for(const auto* key:{"transport_stream_id","original_network_id"})if(a.contains(key) && b.contains(key) && a.at(key)!=b.at(key))return false;return true;
        };
        presets_->defaultName=[this](const auto& value){return text("station_name",{{"frequency",displayDecimal(value.at("frequency_hz").template get<double>()/1e6,3,locale()).toStdString()},{"program",value.value("program_id",0u)}});};
        presets_->selected=[this](const auto& value){selectStation(value);};
        auto* sourceRow=new QHBoxLayout;source_=new QComboBox;source_->setObjectName("dtmb-tv-source");
        source_->addItem(text("source.live"),"live");source_->addItem(text("source.iq"),"iq_file");source_->addItem(text("source.ts"),"transport_file");
        file_=new QLineEdit;file_->setObjectName("dtmb-tv-file");file_->setPlaceholderText(text("file_placeholder"));
        browse_=new QPushButton(text("browse"));start_=new QPushButton(text("start"));start_->setObjectName("dtmb-tv-start");
        stop_=new QPushButton(text("stop"));stop_->setObjectName("dtmb-tv-stop");
        sourceRow->addWidget(source_,1);sourceRow->addWidget(start_);sourceRow->addWidget(stop_);outer->addLayout(sourceRow);
        auto* fileRow=new QHBoxLayout;fileRow->addWidget(file_,1);fileRow->addWidget(browse_);outer->addLayout(fileRow);
        auto* frequencyRow=new QHBoxLayout;frequencyRow->addWidget(label(text("frequency")));
        frequency_=new QLineEdit;frequency_->setObjectName("dtmb-tv-frequency");frequency_->setPlaceholderText(text("frequency_auto"));
        frequency_->setValidator(new QRegularExpressionValidator(decimalInputPattern(locale(),15,6),frequency_));
        center_=new QPushButton(text("use_center"));export_=new QPushButton(text("export"));export_->setObjectName("dtmb-tv-export");
        frequencyRow->addWidget(frequency_,1);frequencyRow->addWidget(center_);frequencyRow->addWidget(export_);outer->addLayout(frequencyRow);
        validation_=label();validation_->setObjectName("dtmb-tv-validation");outer->addWidget(validation_);
        state_=label(text("state.stopped"));state_->setObjectName("dtmb-tv-state");auto font=state_->font();font.setBold(true);state_->setFont(font);outer->addWidget(state_);
        auto* splitter=new QSplitter;image_=new TelevisionImage;splitter->addWidget(image_);
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setMinimumWidth(220);
        auto* side=new QWidget;auto* sideLayout=new QVBoxLayout(side);sideLayout->setContentsMargins(6,0,0,0);scroll->setWidget(side);splitter->addWidget(scroll);
        splitter->setStretchFactor(0,3);splitter->setStretchFactor(1,1);splitter->setSizes({800,300});outer->addWidget(splitter,1);
        auto* programForm=new QFormLayout;programForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        receiverPolicy_=new QComboBox;receiverPolicy_->setObjectName("dtmb-tv-receiver-policy");receiverPolicy_->addItem(text("reception.auto"),false);receiverPolicy_->addItem(text("reception.configured"),true);receiverPolicy_->setCurrentIndex(settings_.value("configured_receiver",false).toBool()?1:0);programForm->addRow(text("reception.policy"),receiverPolicy_);
        receiverSettings_=new QPushButton(text("reception.select_receiver"));receiverSettings_->setObjectName("dtmb-tv-receiver-settings");receiverSettings_->setEnabled(false);programForm->addRow(receiverSettings_);connect(receiverSettings_,&QPushButton::clicked,this,[this]{if(receiverControls_)receiverControls_();});
        connect(receiverPolicy_,&QComboBox::currentIndexChanged,this,[this]{settings_.setValue("configured_receiver",receiverPolicy_->currentData());});
        programs_=combo("dtmb-tv-program");audioTracks_=combo("dtmb-tv-audio-track");videoTracks_=combo("dtmb-tv-video-track");
        programForm->addRow(text("program"),programs_);programForm->addRow(text("audio_track"),audioTracks_);programForm->addRow(text("video_track"),videoTracks_);
        outputs_=combo("dtmb-tv-output");outputs_->addItem(text("device_default"),QString());
        for(const auto& device:MediaPlayback::outputDevices())outputs_->addItem(QString::fromUtf8(device.name),QString::fromStdString(device.id));
        outputs_->setCurrentIndex(std::max(0,outputs_->findData(settings_.value("output_device"))));programForm->addRow(text("device"),outputs_);
        volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("dtmb-tv-volume");volume_->setRange(0,100);volume_->setValue(settings_.value("volume",50).toInt());
        mute_=new QCheckBox(text("mute"));mute_->setObjectName("dtmb-tv-mute");mute_->setChecked(settings_.value("muted",false).toBool());
        programForm->addRow(text("volume"),volume_);programForm->addRow(mute_);sideLayout->addLayout(programForm);
        note_=label(text("compatibility"));sideLayout->addWidget(note_);
        advancedToggle_=new QToolButton;advancedToggle_->setObjectName("dtmb-tv-advanced-toggle");advancedToggle_->setText(text("advanced"));advancedToggle_->setCheckable(true);advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);advancedToggle_->setArrowType(Qt::RightArrow);sideLayout->addWidget(advancedToggle_);
        auto* advanced=new QWidget;auto* advancedLayout=new QVBoxLayout(advanced);advancedLayout->setContentsMargins(0,0,0,0);
        parameters_=new ParameterEditor(nullptr,"dtmb-tv-");parameters_->setObjectName("dtmb-tv-parameters");
        if(auto* form=qobject_cast<QFormLayout*>(parameters_->layout()))form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        advancedLayout->addWidget(parameters_);recommend_=new QPushButton(text("recommend"));recommend_->setObjectName("dtmb-tv-recommend");advancedLayout->addWidget(recommend_);sideLayout->addWidget(advanced);advanced->hide();
        auto* trackToggle=new QToolButton;trackToggle->setObjectName("dtmb-tv-tracks-toggle");trackToggle->setText(text("tracks"));trackToggle->setCheckable(true);sideLayout->addWidget(trackToggle);
        tracks_=new QTableWidget(0,5);tracks_->setObjectName("dtmb-tv-tracks");tracks_->setHorizontalHeaderLabels({text("track.pid"),text("track.kind"),text("track.codec"),text("track.decoder"),text("track.access")});
        tracks_->verticalHeader()->hide();tracks_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);tracks_->setEditTriggers(QAbstractItemView::NoEditTriggers);tracks_->setMaximumHeight(180);tracks_->hide();sideLayout->addWidget(tracks_);
        connect(trackToggle,&QToolButton::toggled,tracks_,&QWidget::setVisible);
        quality_=new QTableWidget(16,2);quality_->setObjectName("dtmb-tv-quality");quality_->setHorizontalHeaderLabels({text("metric"),text("value")});quality_->verticalHeader()->hide();
        quality_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);quality_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);quality_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        quality_->setEditTriggers(QAbstractItemView::NoEditTriggers);quality_->setSelectionMode(QAbstractItemView::NoSelection);quality_->setMinimumHeight(360);sideLayout->addWidget(quality_);sideLayout->addStretch();
        search_=new BroadcastSearch([this](const auto& key,const auto& args){return text(key,args);},"dtmb-tv",474.,698.,8.,5.,this);sideLayout->insertWidget(sideLayout->count()-1,search_);search_->setEnabled(false);
        search_->startProbe=[this](double frequency){
            source_->setCurrentIndex(0);frequency_->setText(displayDecimal(frequency/1e6,6,locale()));wantedStation_=Json::object();
            lease_.reset();stopDecoder();external_=Json::object();acquireLive(frequency);
        };
        search_->stopProbe=[this]{lease_.reset();stopDecoder();player_.setMuted(mute_->isChecked());};
        search_->candidates=[this]{std::vector<Json> result;const auto snapshot=player_.snapshot();for(const auto& program:snapshot.value("programs",Json::array()))if(program.value("ready",false) && !program.value("streams",Json::array()).empty()) {
            auto value=station();if(value.empty())continue;value["program_id"]=program.at("program_id");value["service_name"]=program.value("name",std::string());result.push_back(std::move(value));
        }return result;};
        search_->caption=[this](const Json& value){const auto name=value.value("service_name",std::string());return name.empty()?presets_->defaultName(value):text("scan.program",{{"frequency",value.at("frequency_hz").get<double>()/1e6},{"name",name}});};
        search_->selected=[this](const auto& value){selectStation(value);};
        message_=label();message_->setObjectName("dtmb-tv-message");outer->addWidget(message_);
        diagnostics_=new QPlainTextEdit;diagnostics_->setObjectName("dtmb-tv-diagnostics");diagnostics_->setReadOnly(true);diagnostics_->setMaximumHeight(150);diagnostics_->hide();
        auto* diagnosticToggle=new QToolButton;diagnosticToggle->setText(text("diagnostics"));diagnosticToggle->setCheckable(true);outer->addWidget(diagnosticToggle);outer->addWidget(diagnostics_);
        connect(diagnosticToggle,&QToolButton::toggled,diagnostics_,&QWidget::setVisible);
        connect(advancedToggle_,&QToolButton::toggled,this,[this,advanced](bool show){advanced->setVisible(show);advancedToggle_->setArrowType(show?Qt::DownArrow:Qt::RightArrow);});
        connect(browse_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,text("browse"),file_->text(),text(isTs()?"filter.ts":"filter.iq"));if(!path.isEmpty())file_->setText(path);});
        connect(start_,&QPushButton::clicked,this,[this]{begin();});connect(stop_,&QPushButton::clicked,this,[this]{stop();});
        connect(center_,&QPushButton::clicked,this,[this]{frequency_->clear();refresh();});
        connect(frequency_,&QLineEdit::textChanged,this,[this]{wantedStation_=Json::object();presets_->clearSelection();refresh();});
        connect(file_,&QLineEdit::textChanged,this,[this]{inspectedPath_.clear();recording_.reset();refresh();});
        connect(source_,&QComboBox::currentIndexChanged,this,[this]{pending_.update(parameters_->values());frequency_->clear();populateParameters();refresh();});
        connect(recommend_,&QPushButton::clicked,this,[this]{pending_=dtmbRecommendedParameters();pending_["text_encoding"]="GB18030";populateParameters();refresh();});
        parameters_->changed=[this]{pending_.update(parameters_->values());parameters_->updateDescriptors(descriptors());refresh();};
        connect(programs_,&QComboBox::currentIndexChanged,this,[this]{if(updating_)return;wantedStation_=Json::object();presets_->clearSelection();rebuildTracks();selectTracks();});
        connect(audioTracks_,&QComboBox::currentIndexChanged,this,[this]{if(!updating_)selectTracks();});
        connect(videoTracks_,&QComboBox::currentIndexChanged,this,[this]{if(!updating_)selectTracks();});
        connect(volume_,&QSlider::valueChanged,this,[this](int value){player_.setVolume(value/100.);settings_.setValue("volume",value);});
        connect(mute_,&QCheckBox::toggled,this,[this](bool value){player_.setMuted(value);settings_.setValue("muted",value);});
        connect(outputs_,&QComboBox::currentIndexChanged,this,[this]{const auto id=outputs_->currentData().toString();player_.setOutputDevice(id.toStdString());settings_.setValue("output_device",id);});
        connect(export_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,text("export"),{},text("filter.ts"));if(path.isEmpty())return;
            try{const auto bytes=player_.cachedTransport();if(bytes.empty())throw std::runtime_error("error.no_cache");
                std::ofstream file(path_from_utf8(path.toUtf8().toStdString()),std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!file)throw std::runtime_error("error.export");
                external_=message("exported",{{"bytes",bytes.size()}});
            }catch(const std::exception& error){external_=message(error.what());}refresh();});
        player_.setVolume(volume_->value()/100.);player_.setMuted(mute_->isChecked());player_.setOutputDevice(outputs_->currentData().toString().toStdString());
        populateParameters();timer_.setInterval(33);connect(&timer_,&QTimer::timeout,this,[this]{refresh();});timer_.start();refresh();
    }
    ~DtmbTvPanel() override {stop();}
    LiveResearchTool tool() {
        auto result=LiveResearchTool{this,[this](const LiveResearchContext& context){if(!lease_)context_=context;refresh();},
            [ingress=ingress_](const IqBlockView& block){const auto run=std::atomic_load(&ingress->run);if(run)run->offer(block);},
            [this](const Json& event){return actions(event);},[this]{stop();},[this]{return busy();},
            [this](std::exception_ptr){stop();external_=message("error.source");refresh();},false,[this]{return snapshot();}};
        offer_=result.offer;
        result.attachReception=[this](ReceptionAcquire acquire){acquire_=std::move(acquire);search_->setEnabled(bool(acquire_));if(frequency_->text().isEmpty())frequency_->setText(displayDecimal(474.,6,locale()));refresh();};
        result.attachReceiverControls=[this](auto action){receiverControls_=std::move(action);receiverSettings_->setEnabled(bool(receiverControls_));};
        result.summary=[this]{return search_->active()?search_->summary():busy()?text("status_summary",{{"state",state_->text().toStdString()},{"frequency",displayDecimal(appliedFrequency_/1e6,3,locale()).toStdString()}}):QString();};
        result.health=[this]{
            if(external_.value("message_key",std::string()).starts_with("error."))return render(external_);
            if(const auto run=std::atomic_load(&ingress_->run))if(const auto diagnostic=std::atomic_load(&run->diagnostic))return render(*diagnostic);
            const auto playback=player_.snapshot();const auto state=playback.value("state",std::string());
            if(state=="error")return playback.contains("diagnostic")?render(playback.at("diagnostic")):text("state.error");
            if(state!="stopped" && state!="finished" && playback.contains("audio_error"))return render(playback.at("audio_error"));return QString();
        };return result;
    }
private:
    Json message(const std::string& key,const Json& args=Json::object()) const {Json node={{"message_key",key},{"arguments",args}};catalog_.resolve(node);return node;}
    QString text(const std::string& key,const Json& args=Json::object()) const {Json node={{"label_key",key},{"arguments",args}};catalog_.resolve(node);return localizedText(node.at("label"),language_,QString::fromStdString(key));}
    QString render(const Json& node) const {
        if(!node.is_object())return {};QStringList lines;
        if(node.contains("message"))lines<<localizedText(node.at("message"),language_);
        if(node.contains("cause"))lines<<render(node.at("cause"));
        if(node.contains("detail") && node.at("detail").is_string())lines<<QString::fromUtf8(node.at("detail").get<std::string>());
        lines.removeAll(QString());return lines.join(u'\n');
    }
    QLabel* label(const QString& value={}) {auto* result=new QLabel(value);result->setWordWrap(true);result->setTextFormat(Qt::PlainText);return result;}
    QComboBox* combo(const char* name){auto* value=new QComboBox;value->setObjectName(name);value->setMinimumContentsLength(10);value->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);return value;}
    bool isTs() const {return source_->currentData().toString()=="transport_file";}
    bool isLive() const {return source_->currentData().toString()=="live";}
    Json descriptors() const {Json result=isTs()?Json::array():dtmbReceiveParameters(pending_);result.push_back(transport::broadcastTextParameter());return result;}
    void populateParameters() {
        parameters_->populate(descriptors(),pending_);
        for(auto* box:parameters_->findChildren<QComboBox*>()){box->setMinimumContentsLength(12);box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);}
    }
    std::optional<IqStreamParameters> input(bool actual=false) {
        if(isLive()) {
            if(acquire_ && !actual) {
                bool ok=false;const auto value=canonicalDecimalInput(frequency_->text(),locale()).toDouble(&ok)*1e6;
                if(ok && std::isfinite(value) && value>0 && value<std::ldexp(1.,64))return IqStreamParameters{8000000,value,SampleFormat::ci16_le};
            }
            return context_.receiving?context_.stream:std::nullopt;
        }
        if(isTs())return std::nullopt;
        if(inspectedPath_!=file_->text()) {
            inspectedPath_=file_->text();recording_.reset();fileError_.clear();
            try{recording_=inspect_recording(path_from_utf8(file_->text().toUtf8().toStdString()));}
            catch(const std::exception& error){fileError_=QString::fromUtf8(error.what());}
        }
        if(recording_)return IqStreamParameters{recording_->rate,recording_->frequency,recording_->format};return std::nullopt;
    }
    double target(const IqStreamParameters& source) const {
        if(frequency_->text().trimmed().isEmpty())return source.center_frequency_hz;
        bool ok=false;const auto value=canonicalDecimalInput(frequency_->text(),locale()).toDouble(&ok)*1e6;
        if(!ok || !std::isfinite(value) || value<0)throw std::invalid_argument("error.frequency");return value;
    }
    QString issue(bool actual=false) {
        const auto problems=parameters_->problems();parameters_->showIssues(problems);if(!problems.empty())return problems.front().second;
        if(isLive() && acquire_ && !actual) {
            if(frequency_->text().trimmed().isEmpty()){if(!context_.stream)return text("error.frequency");}
            else {bool valid=false;const auto hz=canonicalDecimalInput(frequency_->text(),locale()).toDouble(&valid)*1e6;if(!valid || !frequency_->hasAcceptableInput() || !std::isfinite(hz) || hz<0 || hz>=std::ldexp(1.,64))return text("error.frequency");}
        }
        if(!isLive()) {
            const auto path=path_from_utf8(file_->text().toUtf8().toStdString());std::error_code error;
            if(!std::filesystem::is_regular_file(path,error))return text("error.file");
        }
        if(isTs())return {};
        const auto source=input(actual);if(!source)return isLive()?text("error.no_source"):text("error.recording",{{"detail",fileError_.toStdString()}});
        try {
            if(isLive() && source->sample_rate<8000000)return text("error.live_rate");
            const auto requested=target(*source);dtmbReceiveOptions(*source,requested,parameters_->values());
        }catch(const std::exception& error){
            const auto detail=dtmbReceiveDiagnostic(error.what());const auto rendered=render(detail);
            return rendered.isEmpty() || rendered==QString::fromUtf8(error.what())?text("error.configuration",{{"detail",error.what()}}):rendered;
        }
        return {};
    }
    void stop() {
        if(search_)search_->cancel();
        lease_.reset();reception_.state="stopped";stopDecoder();
    }
    void stopDecoder() {
        if(auto run=std::atomic_load(&ingress_->run)){run->live=false;if(worker_.joinable())worker_.request_stop();run->changed.notify_all();}
        if(worker_.joinable())worker_.join();player_.stop();if(image_)image_->showFrame({});
    }
    void begin() {
        const auto problem=issue();if(!problem.isEmpty()){validation_->setText(problem);return;}
        if(search_)search_->cancel();
        if(isLive() && acquire_) {
            const auto requested=target(*input());stop();external_=Json::object();reception_={};appliedFrequency_=requested;
            acquireLive(requested);return;
        }
        lease_.reset();beginDecoder();
    }
    void acquireLive(double requested) {
        appliedFrequency_=requested;reception_={};
        try{lease_=acquire_({requested,8000000,8000000,100000000,{},!search_->active() && receiverPolicy_->currentData().toBool()},{[this](const auto& status){
                reception_=status;
                if(status.state=="receiving" && status.stream){context_={true,status.stream};try{beginDecoder();if(search_->active())search_->ready();}catch(const std::exception& e){lease_.reset();stopDecoder();external_=message("error.configuration",{{"detail",e.what()}});if(search_->active())search_->fail(render(external_));}}
                else if(status.state=="starting")stopDecoder();
                else {lease_.reset();stopDecoder();external_=message("error.reception_"+status.state,{{"detail",status.detail}});if(search_->active())search_->fail(render(external_));}
                refresh();
        },offer_});}catch(const std::exception& e){external_=message("error.configuration",{{"detail",e.what()}});if(search_->active())search_->fail(render(external_));}refresh();
    }
    void beginDecoder() {
        const auto problem=issue(true);if(!problem.isEmpty())throw std::invalid_argument(problem.toUtf8().toStdString());
        std::optional<DtmbReceiveOptions> options;if(!isTs()){const auto source=input(true);options=dtmbReceiveOptions(*source,target(*source),parameters_->values());}
        const auto file=path_from_utf8(file_->text().toUtf8().toStdString());const auto recording=recording_;const auto live=isLive(),ts=isTs();
        MediaSelection selection;selection.text_encoding=parameters_->values().at("text_encoding").get<std::string>();
        if(!wantedStation_.empty())selection.program=wantedStation_.value("program_id",0u);
        const auto identity=source_->currentData().toString()+':'+file_->text()+':'+frequency_->text();
        if(wantedStation_.empty() && identity==previousSource_ && programs_->currentIndex()>=0){selection.program=programs_->currentData().toUInt();selection.audio_pid=audioTracks_->currentData().toInt();selection.video_pid=videoTracks_->currentData().toInt();}
        stopDecoder();std::atomic_store(&ingress_->run,std::shared_ptr<TvRun>{});external_=Json::object();player_.start(selection,mediaHelper_);previousSource_=identity;appliedSource_=source_->currentData().toString();if(options)appliedFrequency_=target(options->input);
        player_.setMuted(mute_->isChecked() || (search_ && search_->active()));
        if(player_.snapshot().value("state",std::string())=="error"){refresh();return;}
        auto run=std::make_shared<TvRun>();run->live=live;run->sampleRate=options?options->input.sample_rate:0;std::atomic_store(&ingress_->run,run);applied_=parameters_->values();
        settings_.setValue("parameters",QString::fromUtf8(applied_.dump()));
        worker_=std::jthread([this,run,options,file,recording,live,ts](std::stop_token stop){
            const auto deliver=[&](std::span<const std::uint8_t> bytes){while(!stop.stop_requested() && !player_.offerTransport(bytes)) {
                if(player_.snapshot().value("state",std::string())=="error")throw std::runtime_error("error.media");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }};
            try {
                if(ts) {
                    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("error.file");Bytes bytes(16384);
                    while(!stop.stop_requested() && (input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) || input.gcount())){deliver(std::span(bytes).first(input.gcount()));run->transportRead+=input.gcount();}
                }else {
                    std::uint64_t epoch=0;
                    DtmbStreamDecoder decoder(*options,deliver,[&](const Json& value){
                        if(value.at("epoch").get<std::uint64_t>()!=epoch){
                            epoch=value.at("epoch");const auto selected=player_.snapshot().value("selection",Json::object());
                            player_.start({selected.value("program",0u),selected.value("audio_pid",-1),selected.value("video_pid",-1),applied_.value("text_encoding",std::string("GB18030"))},mediaHelper_);
                        }
                        std::atomic_store(&run->decoder,std::make_shared<const Json>(value));
                    },[&]{return stop.stop_requested();});
                    if(live) {
                        while(!stop.stop_requested()) {
                            QueuedIq block;
                            {std::unique_lock lock(run->mutex);run->changed.wait_for(lock,std::chrono::milliseconds(10),[&]{return stop.stop_requested() || !run->queue.empty();});
                                if(run->queue.empty())continue;block=std::move(run->queue.front());run->queue.pop_front();run->bytes-=block.bytes.size();}
                            decoder.feed({block.bytes,block.first,block.input,block.gap});run->processed+=block.bytes.size()/bytes_per_pair(block.input.format);
                        }
                    }else {
                        std::ifstream input(recording->data_path,std::ios::binary);if(!input)throw std::runtime_error("error.file");
                        const auto stride=bytes_per_pair(recording->format);Bytes bytes(65536*stride);std::uint64_t first=0;
                        while(!stop.stop_requested() && (input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) || input.gcount())) {
                            const auto size=static_cast<std::size_t>(input.gcount());decoder.feed({std::span(bytes).first(size),first,options->input,false});first+=size/stride;run->processed=first;
                        }
                    }
                    if(!stop.stop_requested())decoder.finish();std::atomic_store(&run->decoder,std::make_shared<const Json>(decoder.snapshot()));
                }
                if(!stop.stop_requested())player_.finishInput();
            }catch(const std::exception& error) {
                if(!stop.stop_requested()) {
                    auto diagnostic=message(error.what());
                    if(diagnostic.at("message").is_string() && diagnostic.at("message")==error.what())diagnostic=dtmbReceiveDiagnostic(error.what());
                    if(diagnostic.at("message").is_string() && diagnostic.at("message")==error.what())diagnostic=message("error.processing",{{"detail",error.what()}});
                    std::atomic_store(&run->diagnostic,std::make_shared<const Json>(diagnostic));player_.finishInput();
                }
            }
            run->finishedMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-run->started).count();run->live=false;run->done=true;
        });refresh();
    }
    bool busy() const {
        const auto run=std::atomic_load(&ingress_->run);const auto state=player_.snapshot().value("state",std::string("stopped"));
        return lease_ || (search_ && search_->active()) || (run && !run->done) || (state!="stopped" && state!="finished" && state!="error");
    }
    Json station() const {
        if(!isLive())return Json();bool ok=false;const auto hz=canonicalDecimalInput(frequency_->text(),locale()).toDouble(&ok)*1e6;if(!ok || hz<=0 || !std::isfinite(hz))return Json();
        const auto catalog=player_.snapshot().value("catalog",Json::object()).value("transport",Json::object());
        Json result={{"frequency_hz",std::round(hz)},{"program_id",programs_->currentIndex()>=0?programs_->currentData().toUInt():0u}};
        for(const auto* key:{"transport_stream_id","original_network_id"})if(catalog.contains(key) && catalog.at(key).is_number_integer() && catalog.at(key).get<int>()>=0)result[key]=catalog.at(key);
        return result;
    }
    void selectStation(const Json& value) {
        const auto invalid=[this]{external_=message("error.favorite_invalid");refresh();};
        if(!value.value("frequency_hz",Json()).is_number()){invalid();return;}const auto hz=value.at("frequency_hz").get<double>();
        if(!std::isfinite(hz) || hz<0 || hz>=std::ldexp(1.,64)){invalid();return;}
        for(const auto* key:{"program_id","transport_stream_id","original_network_id"})if(value.contains(key) && (!value.at(key).is_number_integer() || value.at(key).get<double>()<0 || value.at(key).get<double>()>65535)){invalid();return;}
        wantedStation_=value;
        if(lease_ && reception_.state=="receiving" && std::abs(hz-appliedFrequency_)<.5) {
            player_.select({value.value("program_id",0u),-1,-1,pending_.value("text_encoding",std::string("GB18030"))});return;
        }
        source_->setCurrentIndex(0);frequency_->setText(displayDecimal(hz/1e6,6,locale()));wantedStation_=value;refresh();
    }
    void rebuildTracks() {
        const auto player=player_.snapshot();const auto selection=player.value("selection",Json::object());
        const auto selectedAudio=selection.value("audio_pid",-1),selectedVideo=selection.value("video_pid",-1);updating_=true;audioTracks_->clear();videoTracks_->clear();
        audioTracks_->addItem(text("automatic"),-1);videoTracks_->addItem(text("automatic"),-1);tracks_->setRowCount(0);
        for(const auto& program:player.value("programs",Json::array()))if(program.at("program_id")==programs_->currentData().toUInt())
            for(const auto& track:program.value("streams",Json::array())) {
                const auto kind=track.value("media_type",std::string("other"));auto* box=kind=="audio"?audioTracks_:kind=="video"?videoTracks_:nullptr;
                const auto encrypted=track.value("scrambled_packets",0ULL)>0,available=track.value("decoder_available",false);
                const auto row=tracks_->rowCount();tracks_->insertRow(row);
                const QStringList values{QString::number(track.at("pid").get<int>()),text(kind=="audio" || kind=="video"?"track."+kind:"track.other"),QString::fromUtf8(track.value("codec_name",std::string("?"))),text(available?"track.available":"track.unavailable"),text(encrypted?"track.encrypted":track.value("conditional_access",false)?"track.conditional":"track.clear")};
                for(int column=0;column<values.size();++column){auto* item=new QTableWidgetItem(values[column]);item->setToolTip(QString::fromUtf8(track.dump(2)));tracks_->setItem(row,column,item);}
                if(!box)continue;
                const auto caption=text("track",{{"pid",track.at("pid")},{"codec",track.value("codec_name",std::string("?"))}});
                box->addItem(encrypted?text("track_encrypted",{{"track",caption.toStdString()}}):available?caption:text("track_unavailable",{{"track",caption.toStdString()}}),track.at("pid").get<int>());
                if(!available || encrypted)if(auto* model=qobject_cast<QStandardItemModel*>(box->model()))model->item(box->count()-1)->setEnabled(false);
            }
        audioTracks_->setCurrentIndex(std::max(0,audioTracks_->findData(selectedAudio)));videoTracks_->setCurrentIndex(std::max(0,videoTracks_->findData(selectedVideo)));updating_=false;
    }
    void selectTracks() {if(programs_->currentIndex()<0)return;player_.select({programs_->currentData().toUInt(),audioTracks_->currentData().toInt(),videoTracks_->currentData().toInt(),applied_.value("text_encoding",std::string("GB18030"))});}
    void updatePrograms(const Json& player) {
        if(!wantedStation_.empty()) {
            const auto transport=player.value("catalog",Json::object()).value("transport",Json::object());
            for(const auto* key:{"transport_stream_id","original_network_id"})if(wantedStation_.contains(key) && transport.contains(key) && transport.at(key).is_number_integer() && transport.at(key).get<int>()>=0 && wantedStation_.at(key)!=transport.at(key)) {
                stop();external_=message("error.favorite_identity");return;
            }
        }
        const auto programs=player.value("programs",Json::array());if(programs==shownPrograms_)return;
        const auto selected=player.value("selection",Json::object()).value("program",0u);
        const auto actual=player.value("catalog",Json::object()).value("selected_program",0u);shownPrograms_=programs;updating_=true;programs_->clear();
        for(const auto& program:programs) {
            const auto id=program.at("program_id").get<unsigned>();const auto name=program.value("name",std::string());
            programs_->addItem(name.empty()?text("unnamed_program",{{"id",id}}):QString::fromUtf8(name),id);
        }
        auto index=programs_->findData(selected?selected:actual);const bool removed=selected && index<0 && !programs.empty();
        if(index<0 && !programs.empty() && wantedStation_.empty())index=0;programs_->setCurrentIndex(index);updating_=false;rebuildTracks();if(removed && wantedStation_.empty())selectTracks();
        if(!wantedStation_.empty() && selected && index<0 && !programs.empty())external_=message("error.favorite_program_missing");
    }
    void metric(int row,const char* key,const QString& value) {
        if(!quality_->item(row,0))quality_->setItem(row,0,new QTableWidgetItem);quality_->item(row,0)->setText(text(key));if(!quality_->item(row,1))quality_->setItem(row,1,new QTableWidgetItem);
        quality_->item(row,1)->setText(value);
    }
    Json snapshot() const {
        Json result={{"playback",player_.snapshot()},{"requested",applied_},{"applied_source",appliedSource_.toStdString()}};
        result["reception"]={{"state",reception_.state},{"detail",reception_.detail},{"requested",reception_.requested},{"applied",reception_.applied},{"generation",reception_.generation},{"diagnostic",reception_.diagnostic}};
        if(const auto run=std::atomic_load(&ingress_->run)) {
            result["input"]={{"received_pairs",run->received.load()},{"processed_pairs",run->processed.load()},{"rejected_pairs",run->rejected.load()},{"queue_peak_bytes",run->peak.load()},{"budget_bytes",iqBudget},{"done",run->done.load()}};
            {std::lock_guard lock(run->mutex);result["input"]["queued_bytes"]=run->bytes;}
            result["input"]["transport_read_bytes"]=run->transportRead.load();
            auto milliseconds=run->finishedMs.load();if(milliseconds<0)milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-run->started).count();
            const auto rate=milliseconds>0?run->processed.load()*1000./milliseconds:0.;
            result["input"]["elapsed_seconds"]=milliseconds/1000.;result["input"]["mean_processed_pairs_per_second"]=rate;
            result["input"]["mean_realtime_ratio"]=run->sampleRate>0?rate/run->sampleRate:0.;
            if(const auto value=std::atomic_load(&run->decoder))result["receiver"]=*value;
            if(const auto value=std::atomic_load(&run->diagnostic))result["diagnostic"]=*value;
        }
        return result;
    }
    void refresh() {
        if(!parameters_)return;
        presets_->setSaveAvailable(!station().empty());
        const auto problem=issue();validation_->setText(problem);validation_->setVisible(!problem.isEmpty());
        start_->setEnabled(problem.isEmpty());start_->setText(text(busy()?"restart":"start"));stop_->setEnabled(busy());
        file_->setEnabled(!isLive());browse_->setEnabled(!isLive());frequency_->setEnabled(!isTs());center_->setEnabled(!isTs());
        const auto data=snapshot();const auto& playback=data.at("playback");updatePrograms(playback);export_->setEnabled(playback.value("cache_bytes",0ULL)>=564);
        const auto state=playback.value("state",std::string("stopped"));
        if(lease_ && (state=="error" || (data.value("input",Json::object()).value("done",false) && data.contains("diagnostic")))) {
            lease_.reset();if(auto run=std::atomic_load(&ingress_->run)){run->live=false;if(worker_.joinable())worker_.request_stop();run->changed.notify_all();}
            if(search_->active())search_->fail(text("error.source"));
        }
        const auto receiver=data.value("receiver",Json::object());const auto receiving=receiver.value("state",std::string("searching"));
        state_->setText(text(data.contains("diagnostic") && state!="stopped"?"state.error":state=="buffering" && appliedSource_!="transport_file"?"receiver."+receiving:"state."+state));
        if(lease_ && reception_.state=="starting")state_->setText(text("reception_starting"));
        image_->showFrame(player_.videoFrame());image_->placeholder(text("waiting_video"));
        const auto input=data.value("input",Json::object());const bool ts=appliedSource_=="transport_file";
        const auto transport=ts?playback.value("catalog",Json::object()).value("transport",Json::object()):receiver.value("transport",Json::object());
        metric(0,ts?"quality.ts_input":"quality.input",ts?text("transport_read",{{"bytes",input.value("transport_read_bytes",0ULL)}}):text("pairs",{{"processed",input.value("processed_pairs",0ULL)},{"rejected",input.value("rejected_pairs",0ULL)}}));
        metric(1,"quality.queue",text("queue",{{"bytes",ts?playback.value("input_queue_bytes",0ULL):input.value("queued_bytes",0ULL)},{"budget",ts?playback.value("input_budget_bytes",0ULL):iqBudget}}));
        const auto mode=receiver.value("mode",Json::object());metric(2,"quality.mode",mode.empty()?text("unknown"):text("mode",{{"pn",mode.value("pn_symbols",0)},{"si",mode.value("system_info",0)},{"qam",mode.value("qam",std::string())}}));
        metric(3,"quality.ts",text("ts_quality",{{"packets",transport.value("packets",0ULL)},{"crc",transport.value("section_crc_errors",0ULL)},{"gaps",transport.value("continuity_errors",0ULL)}}));
        metric(4,"quality.video",text("video_quality",{{"decoded",playback.value("decoded_video_frames",0ULL)},{"shown",playback.value("presented_video_frames",0ULL)},{"late",playback.value("late_video_frames",0ULL)}}));
        metric(5,"quality.audio",text("audio_quality",{{"written",playback.value("written_audio_frames",0ULL)},{"underruns",playback.value("audio_underruns",0ULL)},{"drains",playback.value("audio_eof_drains",0ULL)},
            {"pending",text(playback.value("audio_output_waiting",false)?"audio_waiting":"audio_not_waiting").toStdString()}}));
        metric(6,"quality.clock",text(playback.value("audio_clock",false)?"clock.audio":"clock.wall"));
        metric(7,"quality.sync",playback.value("audio_clock_video_samples",0ULL)?text("microseconds",{{"value",playback.value("last_video_clock_offset_us",0LL)}}):text("unknown"));
        metric(8,"quality.cache",text("cache",{{"bytes",playback.value("cache_bytes",0ULL)}}));
        metric(9,"quality.decode",text("count",{{"value",playback.value("decode_errors",0ULL)}}));
        metric(10,"quality.unavailable_audio",text("count",{{"value",playback.value("unavailable_audio_frames",0ULL)}}));
        metric(11,"quality.scrambled",text("count",{{"value",transport.value("scrambled_packets",0ULL)}}));
        metric(12,"quality.processing",ts?text("not_applicable"):text("processing_rate",{{"rate",displayDecimal(input.value("mean_processed_pairs_per_second",0.)/1e6,2,locale()).toStdString()},{"speed",displayDecimal(input.value("mean_realtime_ratio",0.),2,locale()).toStdString()}}));
        metric(13,"quality.recovery",text("count",{{"value",receiver.value("lock_losses",0ULL)}}));
        const auto timing=receiver.value("symbol_timing",Json::object());
        metric(14,"quality.timing",timing.empty()?text("unknown"):text("timing_phase",{{"phase",timing.value("timing_phase",std::string())},{"count",timing.value("timing_phase_count",std::string())},{"score",timing.value("timing_score",std::string())}}));
        metric(15,"quality.sample_clock",timing.value("clock_locked",std::string())=="true"?text("estimated_clock",{{"ppm",timing.value("clock_error_ppm",std::string())},{"score",timing.value("clock_score",std::string())}}):text("nominal_clock"));
        QStringList messages;for(const auto* name:{"diagnostic"})if(data.contains(name))messages<<render(data.at(name));
        if(!external_.empty())messages<<render(external_);
        for(const auto* name:{"audio_error","diagnostic","decode_diagnostic"})if(playback.contains(name))messages<<render(playback.at(name));
        messages.removeAll(QString());messages.removeDuplicates();message_->setText(messages.join(u'\n'));message_->setVisible(!messages.empty());
        if(diagnostics_->isVisible())diagnostics_->setPlainText(QString::fromUtf8(data.dump(2,' ',false,Json::error_handler_t::replace)));
    }
    std::vector<LiveResearchAction> actions(const Json& event) {
        if(!event.value("evidence_level",Json()).is_string() || event.at("evidence_level")!="dtmb_frame_structure" || !event.value("center_frequency_hz",Json()).is_number())return {};
        const auto frequency=std::round(event.at("center_frequency_hz").get<double>());if(!std::isfinite(frequency) || frequency<0 || frequency>=std::ldexp(1.,64))return {};
        return {{"open-television",text("event_open"),{},true,[this,frequency]{source_->setCurrentIndex(0);frequency_->setText(displayDecimal(frequency/1e6,6,locale()));refresh();}},
            {"favorite-television",text("event_favorite"),{},true,[this,frequency]{presets_->saveValue({{"frequency_hz",frequency},{"program_id",0}});}}};
    }
    QString language_;TextCatalog catalog_;QSettings settings_;LiveResearchContext context_;MediaPlayback player_;QTimer timer_;std::filesystem::path mediaHelper_;
    Json pending_,applied_=Json::object(),shownPrograms_=Json::array(),external_=Json::object();bool updating_=false;
    std::optional<ReplayInfo> recording_;QString inspectedPath_,fileError_,previousSource_,appliedSource_;std::jthread worker_;std::shared_ptr<TvIngress> ingress_=std::make_shared<TvIngress>();
    QComboBox *source_=nullptr,*programs_=nullptr,*audioTracks_=nullptr,*videoTracks_=nullptr,*outputs_=nullptr;
    QLineEdit *file_=nullptr,*frequency_=nullptr;QPushButton *browse_=nullptr,*start_=nullptr,*stop_=nullptr,*center_=nullptr,*export_=nullptr,*recommend_=nullptr;
    QLabel *state_=nullptr,*validation_=nullptr,*note_=nullptr,*message_=nullptr;QSlider* volume_=nullptr;QCheckBox* mute_=nullptr;
    QToolButton* advancedToggle_=nullptr;ParameterEditor* parameters_=nullptr;TelevisionImage* image_=nullptr;QTableWidget *quality_=nullptr,*tracks_=nullptr;QPlainTextEdit* diagnostics_=nullptr;
    BroadcastPresets* presets_=nullptr;ReceptionAcquire acquire_;std::shared_ptr<ReceptionLease> lease_;ReceptionStatus reception_;IqBlockSubscriber offer_;double appliedFrequency_=0;Json wantedStation_=Json::object();
    BroadcastSearch* search_=nullptr;
    QComboBox* receiverPolicy_=nullptr;
    QPushButton* receiverSettings_=nullptr;std::function<void()> receiverControls_;
};
}
LiveResearchProvider dtmbTvResearchProvider(std::filesystem::path helper){return {"dtmb-television",[helper=std::move(helper)](const QString& language,QWidget* parent){return (new DtmbTvPanel(language,parent,helper))->tool();}};}
}
