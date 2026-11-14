#include "laboratory.hpp"
#include "recorded_worker.hpp"
#include "trace_view.hpp"
#include "plane_view.hpp"
#include "capture_controls.hpp"
#include "analysis_save_worker.hpp"
#include "channel_panel.hpp"
#include "window_panel.hpp"
#include "model_panel.hpp"
#include "recovery_panel.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <fstream>
#include <array>
#include <limits>
#include <numbers>
#include <charconv>

namespace signalscope::desktop {
namespace {
std::uint64_t sampleIndex(const Json& value) {
    const auto input=value.get<std::string>();std::uint64_t result=0;
    const auto parsed=std::from_chars(input.data(),input.data()+input.size(),result);
    if(input.empty() || parsed.ec!=std::errc{} || parsed.ptr!=input.data()+input.size())throw std::invalid_argument("Invalid decimal uint64 sample position");
    return result;
}
class Laboratory final:public QWidget {
public:
    Laboratory(const QString& language,QWidget* parent):QWidget(parent),language_(language),catalog_(TextCatalog::resource("waveform-laboratory")) {
        setObjectName("waveform-laboratory");setWindowTitle(text("title"));setMinimumSize(260,240);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(6,6,6,6);layout->setSpacing(5);
        auto* toolbar=new QToolBar(this);toolbar->setMovable(false);toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        const auto smallIcon=style()->pixelMetric(QStyle::PM_SmallIconSize);toolbar->setIconSize(QSize(smallIcon,smallIcon));
        open_recording_=toolbar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),text("open.recording"),this,[this]{chooseRecording();});open_recording_->setObjectName("waveform-open-recording");
        open_analysis_=toolbar->addAction(text("open.analysis"),this,[this]{chooseAnalysis();});open_analysis_->setObjectName("waveform-open-analysis");
        save_=toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton),text("save.analysis"),this,[this]{saveAnalysis();});save_->setObjectName("waveform-save-analysis");save_->setEnabled(false);
        cancel_=toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserStop),text("cancel"),this,[this]{cancelAnalysis();});cancel_->setObjectName("waveform-cancel-analysis");cancel_->setEnabled(false);layout->addWidget(toolbar);
        source_=label(text("empty"));source_->setObjectName("waveform-source");layout->addWidget(source_);
        fileRange_=new QWidget;auto* ranges=new QGridLayout(fileRange_);ranges->setContentsMargins(0,0,0,0);fileRange_->hide();
        first_=new QLineEdit("0");count_=new QLineEdit;first_->setObjectName("waveform-file-first");count_->setObjectName("waveform-file-count");
        const auto integer=QRegularExpression("[0-9]{1,20}");first_->setValidator(new QRegularExpressionValidator(integer,first_));count_->setValidator(new QRegularExpressionValidator(integer,count_));
        ranges->addWidget(label(text("range.first")),0,0);ranges->addWidget(label(text("range.count")),0,1);
        ranges->addWidget(first_,1,0);ranges->addWidget(count_,1,1);
        applyRange_=new QPushButton(text("range.apply"));applyRange_->setObjectName("waveform-apply-range");applyRange_->setEnabled(false);ranges->addWidget(applyRange_,1,2);
        connect(applyRange_,&QPushButton::clicked,this,[this]{applyFileRange();});layout->addWidget(fileRange_);
        auto* overviewHeaderWidget=new QWidget;auto* overviewHeader=new QHBoxLayout(overviewHeaderWidget);overviewHeader->setContentsMargins(0,0,0,0);overviewHeader->addWidget(label(text("overview")),1);
        auto* overviewToggle=new QCheckBox(text("overview.show"));overviewToggle->setChecked(QSettings().value("waveform/overview",true).toBool());overviewHeader->addWidget(overviewToggle);layout->addWidget(overviewHeaderWidget);
        overview_=new TraceView(this);overview_->setObjectName("waveform-overview");overview_->setAccessibleName(text("overview"));overview_->setFixedHeight(76);overview_->setVisible(overviewToggle->isChecked());layout->addWidget(overview_);
        connect(overviewToggle,&QCheckBox::toggled,this,[this](bool show){overview_->setVisible(show);QSettings().setValue("waveform/overview",show);});
        auto* scaleWidget=new QWidget;auto* scaleRow=new QHBoxLayout(scaleWidget);scaleRow->setContentsMargins(0,0,0,0);viewRange_=label({});viewRange_->setObjectName("waveform-view-range");scaleRow->addWidget(viewRange_,1);
        scale_=new QComboBox;scale_->setObjectName("waveform-scale");scale_->setPlaceholderText(text("scale.custom"));scale_->addItem(text("scale.full"),0.);scale_->addItem(quantity(4.096,"ms",3),.004096);scale_->addItem(quantity(1.024,"ms",3),.001024);scale_->addItem(quantity(32,"µs",0),.000032);
        scaleRow->addWidget(scale_);layout->addWidget(scaleWidget);
        connect(scale_,qOverload<int>(&QComboBox::activated),this,[this](int){setScale();});
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* body=new QWidget;auto* plots=new QVBoxLayout(body);plots->setContentsMargins(0,0,0,0);plots->setSpacing(4);
        const std::array<const char*,4> titles{"plot.iq","plot.amplitude","plot.phase","plot.frequency"};
        for(std::size_t n=0;n<views_.size();++n) {
            auto* header=new QHBoxLayout;header->addWidget(label(text(titles[n])),1);
            if(n==2){phase_=new QComboBox;phase_->setObjectName("waveform-phase-mode");phase_->addItem(text("phase.unwrapped"),"unwrapped_phase");phase_->addItem(text("phase.wrapped"),"phase");header->addWidget(phase_);connect(phase_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshTraces(true);});}
            if(n==3){smooth_=new QCheckBox(text("frequency.smoothed"));smooth_->setObjectName("waveform-show-smoothed");header->addWidget(smooth_);connect(smooth_,&QCheckBox::toggled,this,[this]{refreshTraces(true);});}
            plots->addLayout(header);views_[n]=new TraceView;views_[n]->setObjectName("waveform-track-"+QString::number(n));views_[n]->setAccessibleName(text(titles[n]));views_[n]->setMinimumHeight(n==0?90:70);plots->addWidget(views_[n]);
            views_[n]->rangeChanged=[this](auto range){changeViewRange(range);};views_[n]->cursorChanged=[this](auto sample,bool a){if(a)a_=sample;else b_=sample;refreshCursors();};
        }
        overview_->rangeChanged=[this](auto range){if(synchronizing_)return;changeViewRange(range);QScopedValueRollback guard(synchronizing_,true);overview_->resetRange();};
        scroll->setWidget(body);tracks_=new QTabWidget;tracks_->setObjectName("waveform-track-tabs");tracks_->addTab(scroll,text("channel.raw_tab"));
        channel_=new WaveformChannelPanel(language_);tracks_->addTab(channel_,text("channel.tab"));layout->addWidget(tracks_,1);
        window_=new WaveformWindowPanel(language_);tracks_->addTab(window_,text("window.title"));
        model_=new WaveformModelPanel(language_);tracks_->addTab(model_,text("model.title"));model_->apply=[this]{compareModel();};
        recovery_=new WaveformRecoveryPanel(language_);tracks_->addTab(recovery_,text("recovery.title"));recovery_->apply=[this]{recoverCurrent();};
        recovery_->locate=[this](auto ranges,auto input,bool original){locateRecovery(ranges,std::move(input),original);};
        recovery_->exportResult=[this](bool binary){exportRecovery(binary);};
        connect(tracks_,&QTabWidget::currentChanged,this,[this,overviewHeaderWidget,overviewToggle,scaleWidget](int index){const bool timeTracks=index<2;fileRange_->setVisible(timeTracks && bool(recording_));overviewHeaderWidget->setVisible(timeTracks);overview_->setVisible(timeTracks && overviewToggle->isChecked());scaleWidget->setVisible(timeTracks);});
        model_->sourceRangeChanged=[this](auto range){changeViewRange(range);};
        model_->sourceCursorChanged=[this](auto sample,bool a){if(a)a_=sample;else b_=sample;refreshCursors();};
        window_->apply=[this]{inspectWindow(true);};
        window_->bandSelected=[this](const auto& selection,const auto& analysis){
            const auto center=analysis->processed?analysis->processed->center_frequency_hz:analysis->source->stream.center_frequency_hz;
            channel_->selectBand(center-analysis_->source->stream.center_frequency_hz+(selection.low_offset_hz+selection.high_offset_hz)/2,selection.high_offset_hz-selection.low_offset_hz);
            const auto first=analysis->source_position(selection.range.first).sample,last=analysis->source_position(selection.range.first+selection.range.count-1).sample;
            changeViewRange({first,last-first+1});tracks_->setCurrentIndex(1);
        };
        window_refresh_.setSingleShot(true);window_refresh_.setInterval(100);connect(&window_refresh_,&QTimer::timeout,this,[this]{inspectWindow(false);});
        channel_->apply=[this]{reanalyze();};channel_->sourceRangeChanged=[this](auto range){changeViewRange(range);};
        channel_->sourceCursorChanged=[this](auto sample,bool a){if(a)a_=sample;else b_=sample;refreshCursors();};
        channel_->revealOriginal=[this](auto position){revealOriginal(position);};
        status_=label(text("empty"));status_->setObjectName("waveform-status");layout->addWidget(status_);
        createInspector(parent);
        refreshActions();
        timer_.setInterval(40);connect(&timer_,&QTimer::timeout,this,[this]{poll();});timer_.start();
    }
    ~Laboratory() override {worker_.cancel();save_worker_.cancel();delete inspector_.data();}
    LiveResearchTool tool() {
        LiveResearchTool result;result.widget=this;result.monitorBound=false;result.workspaceLayoutTitle=text("layout.research");
        result.panels.push_back({"inspector",text("inspector"),inspector_,false,Qt::RightDockWidgetArea,this,Qt::Horizontal});
        result.recordingActions=[this](const ResearchRecordingContext& input) {
            return std::vector<LiveResearchAction>{{"inspect-recording",text("event.open"),input.metadata.empty()?text("error.missing_source"):saving_?text("save.saving"):QString(),!input.metadata.empty() && !saving_,[this,input]{channel_->resetFileScope();event_=input.event;open(input.metadata,input.range?std::optional<SignalSampleRange>(SignalSampleRange{input.range->first,input.range->count}):std::nullopt);},{this,inspector_}}};
        };
        result.stop=[this]{stop();};result.busy=[this]{return busy_ || save_worker_.busy() || capture_->busy();};
        result.failure=[this](std::exception_ptr failure){stop();last_error_=text("error.operation");status_->setText(last_error_);failure_=failure;};
        result.attachReception=[this](auto acquire){capture_->attachReception(std::move(acquire));};
        result.attachReceiverControls=[this](auto callback){capture_->attachReceiverControls(std::move(callback));};
        result.sourceChanged=[this](const auto& context){capture_->sourceChanged(!context.stream && analysis_?LiveResearchContext{false,analysis_->source->stream}:context);};
        result.summary=[this]{return capture_->summary();};result.snapshot=[this]{return snapshot();};result.health=[this]{return last_error_.isEmpty()?capture_->health():last_error_;};return result;
    }
private:
    QString text(const std::string& key,const Json& arguments=Json::object()) const {
        Json value={{"label_key",key},{"arguments",arguments}};catalog_.resolve(value);return localizedText(value.at("label"),language_,QString::fromStdString(key));
    }
    QLabel* label(const QString& value) {auto* item=new QLabel(value);item->setTextFormat(Qt::PlainText);item->setWordWrap(true);return item;}
    std::string number(double value,int precision=3) const {return displayDecimal(value,precision,locale()).toStdString();}
    QString quantity(double value,const char* unit,int precision=3) const {return std::isfinite(value)?text("quantity",{{"value",number(value,precision)},{"unit",unit}}):text("unavailable");}
    void createInspector(QWidget* parent) {
        inspector_=new QWidget(parent);inspector_->setObjectName("waveform-inspector");inspector_->setWindowTitle(text("inspector"));inspector_->setMinimumWidth(220);
        auto* layout=new QVBoxLayout(inspector_);layout->setContentsMargins(6,6,6,6);
        auto* tabs=new QTabWidget;tabs->setObjectName("waveform-inspector-tabs");layout->addWidget(tabs);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* measure=new QWidget;auto* contents=new QVBoxLayout(measure);contents->setContentsMargins(4,4,4,4);
        auto* form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        const std::array<const char*,15> keys{"measure.a","measure.b","measure.delta_time","measure.delta_samples","measure.delta_frequency","measure.delta_phase","sample.index","sample.file_index","sample.i","sample.q","sample.amplitude","sample.phase","sample.unwrapped_phase","sample.phase_difference","sample.frequency"};
        for(std::size_t n=0;n<readouts_.size();++n){readouts_[n]=label(text("unavailable"));readouts_[n]->setObjectName(QString::fromLatin1(keys[n]));form->addRow(text(keys[n]),readouts_[n]);}contents->addLayout(form);
        validity_=label({});validity_->setObjectName("waveform-validity");contents->addWidget(validity_);
        auto* mark=new QPushButton(text("measure.mark_a"));connect(mark,&QPushButton::clicked,this,[this]{a_=b_;refreshCursors();});contents->addWidget(mark);
        auto* options=new QGroupBox(text("options.title"));auto* optionLayout=new QFormLayout(options);
        floor_=new QDoubleSpinBox;floor_->setDecimals(9);floor_->setRange(0,1);floor_->setSingleStep(.01);floor_->setValue(.05);floor_->setObjectName("waveform-amplitude-floor");
        averaging_=new QSpinBox;averaging_->setRange(1,100001);averaging_->setValue(9);averaging_->setObjectName("waveform-smoothing-samples");
        optionLayout->addRow(text("options.relative_floor"),floor_);optionLayout->addRow(text("options.smoothing"),averaging_);
        apply_options_=new QPushButton(text("options.apply"));apply_options_->setObjectName("waveform-apply-options");connect(apply_options_,&QPushButton::clicked,this,[this]{reanalyze();});optionLayout->addRow(apply_options_);contents->addWidget(options);
        contents->addWidget(label(text("measure.help")));contents->addStretch();scroll->setWidget(measure);tabs->addTab(scroll,text("tab.measure"));
        auto* captureScroll=new QScrollArea;captureScroll->setWidgetResizable(true);captureScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        capture_=new WaveformCaptureControls(language_);captureScroll->setWidget(capture_);tabs->addTab(captureScroll,text("capture.tab"));
        capture_->canAnalyze=[this]{return !busy_ && !saving_ && !save_dialog_;};capture_->windowReady=[this](auto source,bool){analyzeCapture(std::move(source));};
        auto* channelScroll=new QScrollArea;channelScroll->setWidgetResizable(true);channelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);channelScroll->setWidget(channel_->controls());tabs->addTab(channelScroll,text("channel.tab"));
        auto* windowScroll=new QScrollArea;windowScroll->setWidgetResizable(true);windowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);windowScroll->setWidget(window_->controls());tabs->addTab(windowScroll,text("window.title"));
        auto* modelScroll=new QScrollArea;modelScroll->setWidgetResizable(true);modelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);modelScroll->setWidget(model_->controls());tabs->addTab(modelScroll,text("model.title"));
        auto* recoveryScroll=new QScrollArea;recoveryScroll->setWidgetResizable(true);recoveryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);recoveryScroll->setWidget(recovery_->controls());tabs->addTab(recoveryScroll,text("recovery.title"));
        connect(tracks_,&QTabWidget::currentChanged,this,[tabs,channelScroll,windowScroll,modelScroll,recoveryScroll](int index){if(index==1)tabs->setCurrentWidget(channelScroll);if(index==2)tabs->setCurrentWidget(windowScroll);if(index==3)tabs->setCurrentWidget(modelScroll);if(index==4)tabs->setCurrentWidget(recoveryScroll);if(index==0 && (tabs->currentWidget()==channelScroll || tabs->currentWidget()==windowScroll || tabs->currentWidget()==modelScroll || tabs->currentWidget()==recoveryScroll))tabs->setCurrentIndex(0);});
        auto* planePage=new QWidget;auto* planeLayout=new QVBoxLayout(planePage);planeLayout->addWidget(label(text("plane.axes")));
        plane_=new TracePlaneView;plane_->setObjectName("waveform-plane");plane_->setAccessibleName(text("plane.title"));planeLayout->addWidget(plane_,1);planeLayout->addWidget(label(text("plane.help")));tabs->addTab(planePage,text("plane.title"));
        provenance_=new QPlainTextEdit;provenance_->setReadOnly(true);provenance_->setObjectName("waveform-provenance");tabs->addTab(provenance_,text("tab.source"));
    }
    WaveformOptions options() const {WaveformOptions value;value.relative_amplitude_floor=floor_->value();value.frequency_smoothing_samples=static_cast<unsigned>(averaging_->value());return value;}
    void cancelAnalysis(){capture_->pauseFollowing();worker_.cancel();save_worker_.cancel();window_refresh_.stop();pending_window_=false;pending_reveal_.reset();pending_capture_.reset();restoring_.reset();busy_=false;refreshActions();if(analysis_)status_->setText(text("canceled.keep"));else status_->setText(text("canceled"));}
    void stop(){capture_->stop();cancelAnalysis();}
    void chooseRecording() {
        QStringList filters,allPatterns;
        for(const auto& provider:signal_source_providers()) {
            Json label={{"label_key",provider.label_key},{"label",provider.id}};try{TextCatalog::resource(provider.label_domain).resolve(label);}catch(...){}QStringList patterns;
            for(const auto& suffix:provider.filename_suffixes)patterns.push_back("*"+QString::fromStdString(suffix));allPatterns.append(patterns);
            filters.push_back(text("filter.source",{{"name",localizedText(label.at("label"),language_).toStdString()},{"patterns",patterns.join(' ').toStdString()}}));
        }
        filters.prepend(text("filter.source",{{"name",text("filter.all_sources").toStdString()},{"patterns",allPatterns.join(' ').toStdString()}}));
        const auto selected=QFileDialog::getOpenFileName(this,text("open.recording"),QSettings().value("waveform/last_recording").toString(),filters.join(";;"));
        if(!selected.isEmpty()){event_=Json::object();open(QFileInfo(selected).filesystemFilePath());}
    }
    void open(const std::filesystem::path& path,std::optional<SignalSampleRange> range={},WaveformSourceIdentity expected={},bool restoring=false) {
        if(saving_)return;
        window_refresh_.stop();pending_window_=false;
        pending_reveal_.reset();
        if(!restoring && (!analysis_ || path!=analysis_->source->recording_path))channel_->resetFileScope();
        capture_->pauseFollowing();
        if(!restoring)restoring_.reset();
        RecordedWaveformRequest request;request.metadata=path;request.range=range;request.options=options();request.expected=std::move(expected);request.channel=channel_->draft();
        if(!attachChannelFile(request,restoring))return;
        attachWindow(request,restoring);
        if(restoring && restoring_)request.model=WaveformModelPanel::readConfiguration(restoring_->value("model",Json()));
        if(restoring && restoring_){request.recovery=WaveformRecoveryPanel::readConfiguration(restoring_->value("recovery",Json()));if(request.recovery)request.recovery->event=restoring_->value("event",Json::object());}
        generation_=worker_.request(std::move(request));busy_=true;failure_={};last_error_.clear();save_->setEnabled(false);cancel_->setEnabled(true);status_->setText(text("loading"));refreshActions();
    }
    void applyFileRange() {
        if(!analysis_)return;bool firstValid=false,countValid=false;
        const auto first=first_->text().toULongLong(&firstValid),count=count_->text().toULongLong(&countValid);
        if(!firstValid || !countValid || !count || !first_->hasAcceptableInput() || !count_->hasAcceptableInput()){status_->setText(text("error.range"));return;}
        event_=loaded_event_;
        open(analysis_->source->recording_path,SignalSampleRange{first,count});
    }
    void reanalyze() {
        if(!analysis_ || saving_)return;
        window_refresh_.stop();pending_window_=false;
        pending_reveal_.reset();
        restoring_.reset();
        RecordedWaveformRequest request;request.metadata=analysis_->source->recording_path;request.cached=analysis_->source;request.recording=recording_;request.cached_identity=identity_;request.options=options();request.preserve_view=true;request.channel=channel_->draft();
        if(!attachChannelFile(request))return;
        attachWindow(request);
        generation_=worker_.request(std::move(request));busy_=true;last_error_.clear();save_->setEnabled(false);cancel_->setEnabled(true);status_->setText(text("analyzing"));refreshActions();
    }
    void analyzeCapture(std::shared_ptr<const SignalWindow> source) {
        if(saving_ || save_dialog_){pending_capture_=std::move(source);return;}
        window_refresh_.stop();pending_window_=false;
        pending_reveal_.reset();
        channel_->resetFileScope();
        restoring_.reset();event_=Json::object();RecordedWaveformRequest request;request.cached=std::move(source);request.options=options();request.channel=channel_->draft();
        attachWindow(request);
        generation_=worker_.request(std::move(request));busy_=true;last_error_.clear();save_->setEnabled(false);cancel_->setEnabled(true);status_->setText(text("analyzing"));refreshActions();
    }
    void poll() {
        pollSave();
        const auto done=worker_.takeLatest();if(!done || done->generation!=generation_)return;
        busy_=false;cancel_->setEnabled(false);
        if(done->failure){restoring_.reset();pending_reveal_.reset();window_refresh_.stop();pending_window_=false;failure_=done->failure;last_error_=done->error_key.starts_with("recovery.")?recovery_->failureText(done->failure,done->error_key):model_->failureText(done->failure,done->error_key);status_->setText(last_error_);refreshActions();return;}
        const bool had_channel=current_result_ && bool(current_result_->channel_analysis);current_result_=done;window_->setResult(done->research,done->research_processed);
        const auto previous_model=model_->result();model_->setResult(done->model,done->model_request);
        const auto previous_recovery=recovery_->result();recovery_->setResult(done->recovery,done->recovery_request);
        if(done->research_only){model_->setSourceCursors(a_,b_);last_error_.clear();status_->setText(text(previous_recovery!=done->recovery?"recovery.complete":previous_model!=done->model?"model.complete":"window.complete"));refreshProvenance();if(pending_window_){pending_window_=false;window_refresh_.start();}refreshActions();return;}
        pending_window_=false;
        const auto previous=views_[0]->range();analysis_=done->analysis;recording_=done->recording;identity_=done->identity;file_range_=done->file_range;
        if(!analysis_ || analysis_->source->segments.empty())return;
        applied_options_=done->options;channel_->setRecordedSource(recording_,file_range_,bool(restoring_));channel_->setResult(done->channel_analysis,done->channel,done->preserve_view);
        model_->setInputs(done->analysis,done->channel_analysis);
        recovery_->setInputs(done->analysis,done->channel_analysis);
        if(!done->preserve_view)loaded_event_=event_;
        first_->setText(QString::number(file_range_.first));count_->setText(QString::number(file_range_.count));fileRange_->setVisible(bool(recording_) && tracks_->currentIndex()<2);refreshActions();
        const auto& source=*analysis_->source;
        source_->setText(text(recording_?"source.recording":"source.capture",{{"name",path_to_utf8(source.recording_path.filename())},{"rate",number(source.stream.sample_rate/1e6,3)},{"center",number(source.stream.center_frequency_hz/1e6,6)},{"encoding",sample_datatype(source.stream.format)}}));
        source_->setToolTip(QString::fromStdString(recording_?path_to_utf8(source.recording_path):source.source_id));
        if(recording_)QSettings().setValue("waveform/last_recording",QString::fromStdString(path_to_utf8(source.recording_path)));
        capture_->sourceChanged({false,source.stream});
        if(!done->preserve_view){a_=source.first_sample();b_=source.first_sample()+std::min<std::uint64_t>(source.sample_count()-1,source.sample_count()/2);}
        refreshTraces(false);changeViewRange(done->preserve_view?previous:SignalSampleRange{source.first_sample(),source.end_sample()-source.first_sample()},!done->channel_file_range);refreshCursors(!done->preserve_view || !had_channel);
        refreshProvenance();
        status_->setText(text(recording_?"loaded":"capture.loaded",{{"count",source.sample_count()},{"duration",number(1000.*(source.end_sample()-source.first_sample())/source.stream.sample_rate,3)},{"total",recording_?recording_->storage.pairs:0}}));
        if(restoring_){const auto state=std::move(*restoring_);restoring_.reset();restoreView(state);}
        if(pending_reveal_) {
            const auto reveal=std::move(*pending_reveal_);pending_reveal_.reset();a_=reveal.a;b_=reveal.sample;
            const auto count=std::min<std::uint64_t>(std::max<std::uint64_t>(1,source.stream.sample_rate*32/1000000),source.end_sample()-source.first_sample());
            const auto first=reveal.sample-std::min(reveal.sample-source.first_sample(),count/2);changeViewRange({std::min(first,source.end_sample()-count),count},false);refreshCursors();
            channel_->restoreView(reveal.channel);window_->restoreView(reveal.window);model_->restoreView(reveal.model);recovery_->restoreView(reveal.recovery);queueWindowRefresh();status_->setText(text("channel.preview_loaded"));refreshProvenance();
        }
    }
    void revealOriginal(SourceSamplePosition position) {
        if(!analysis_ || busy_ || saving_)return;
        if(locate_signal_sample(*analysis_->source,position.sample)) {
            const auto saved=channel_->configuration();b_=position.sample;const auto count=std::min<std::uint64_t>(std::max<std::uint64_t>(1,analysis_->source->stream.sample_rate*32/1000000),analysis_->source->end_sample()-analysis_->source->first_sample());
            const auto first=position.sample-std::min(position.sample-analysis_->source->first_sample(),count/2);changeViewRange({std::min(first,analysis_->source->end_sample()-count),count},false);refreshCursors();channel_->restoreView(saved);queueWindowRefresh();tracks_->setCurrentIndex(0);return;
        }
        if(!recording_ || !current_result_ || !current_result_->channel_file_range)return;
        const auto stored=recording_->stored_index(position.sample);if(!stored){status_->setText(text("sample.missing"));return;}
        const auto count=std::min<std::uint64_t>(16384,recording_->storage.pairs),first=std::min(*stored-std::min(*stored,count/2),recording_->storage.pairs-count);
        RecordedWaveformRequest request;request.metadata=recording_->metadata_path;request.range=SignalSampleRange{first,count};request.expected.metadata_sha256=identity_.metadata_sha256;
        request.options=applied_options_;request.channel=current_result_->channel;request.channel_file_range=current_result_->channel_file_range;request.expected_channel_source=current_result_->channel_source_identity;request.preserve_view=true;
        if(const auto result=window_->result()){request.research_options=result->options;request.research_processed=window_->processed();if(request.research_processed)request.research_range=result->range;}
        if(model_->appliedRequest() && model_->appliedRequest()->processed)request.model=model_->appliedRequest();
        if(recovery_->appliedRequest() && recovery_->appliedRequest()->processed)request.recovery=recovery_->appliedRequest();
        capture_->pauseFollowing();window_refresh_.stop();pending_window_=false;restoring_.reset();
        pending_reveal_=OriginalReveal{position.sample,a_,channel_->configuration(),window_->configuration(),model_->configuration(),recovery_->configuration()};
        generation_=worker_.request(std::move(request));busy_=true;last_error_.clear();status_->setText(text("loading"));refreshActions();tracks_->setCurrentIndex(0);
    }
    void attachWindow(RecordedWaveformRequest& request,bool restoring=false) {
        if(restoring && restoring_) {
            const auto config=restoring_->value("window",Json::object());
            if(config.value("enabled",false)){request.research_options=window_->draft();request.research_processed=window_->draftProcessed();const auto& range=config.at("range");request.research_range=SignalSampleRange{sampleIndex(range.at("first")),sampleIndex(range.at("count"))};}
        }else if(const auto previous=window_->result()){request.research_options=previous->options;request.research_processed=window_->processed();}
    }
    bool attachChannelFile(RecordedWaveformRequest& request,bool restoring=false) {
        try {
            if(restoring && restoring_) {
                const auto channel=restoring_->value("channel",Json::object());
                if(channel.value("enabled",false) && channel.at("version")==2) {
                    const auto& input=channel.at("input");request.channel_file_range=SignalSampleRange{sampleIndex(input.at("first_file_sample")),sampleIndex(input.at("sample_count"))};
                    const auto& identity=input.at("identity");if(identity.at("format")!="org.amc.normalized-recorded-range" || identity.at("version")!=1)throw std::invalid_argument("Unsupported recorded input identity");
                    const auto valid=[](const std::string& value){return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});};
                    request.expected_channel_source={identity.at("metadata_sha256").get<std::string>(),identity.at("normalized_source_sha256").get<std::string>()};
                    if(!valid(request.expected_channel_source.metadata_sha256) || !valid(request.expected_channel_source.normalized_source_sha256))throw std::invalid_argument("Invalid recorded input identity");
                }
            }else {
                request.channel_file_range=channel_->draftFileRange();
                if(request.channel_file_range && current_result_ && request.channel_file_range==current_result_->channel_file_range)request.expected_channel_source=current_result_->channel_source_identity;
            }
            return true;
        }catch(...){worker_.cancel();busy_=false;restoring_.reset();last_error_=text("channel.error.configuration");status_->setText(last_error_);refreshActions();return false;}
    }
    void inspectWindow(bool applyDraft) {
        if(!current_result_)return;
        if(saving_ || save_dialog_){pending_window_=true;return;}
        if(busy_){pending_window_=true;return;}
        const auto previous=window_->result();const auto selected=applyDraft?window_->draft():previous?std::optional<WindowResearchOptions>(previous->options):std::nullopt;
        if(!selected){if(applyDraft){window_->setResult({});auto copy=std::make_shared<RecordedWaveformCompletion>(*current_result_);copy->research.reset();current_result_=std::move(copy);refreshProvenance();}refreshActions();return;}
        const auto processed=applyDraft?window_->draftProcessed():window_->processed();const auto range=processed?channel_->range():views_[0]->range();
        if(!applyDraft && previous && previous->range==range){refreshActions();return;}
        RecordedWaveformRequest request;request.completed=current_result_;request.research_options=selected;request.research_processed=processed;request.research_range=range;
        generation_=worker_.request(std::move(request));busy_=true;last_error_.clear();status_->setText(text("analyzing"));refreshActions();
    }
    void compareModel() {
        if(!current_result_ || busy_ || saving_ || save_dialog_)return;
        model_->setSelection(views_[0]->range(),channel_->range());
        try {
            const auto selected=model_->draft();
            if(!selected){model_->setResult({},{});auto copy=std::make_shared<RecordedWaveformCompletion>(*current_result_);copy->model.reset();copy->model_request.reset();current_result_=std::move(copy);refreshProvenance();refreshActions();return;}
            capture_->pauseFollowing();pending_window_=pending_window_ || window_refresh_.isActive();window_refresh_.stop();restoring_.reset();pending_reveal_.reset();
            RecordedWaveformRequest request;request.completed=current_result_;request.model=selected;
            generation_=worker_.request(std::move(request));busy_=true;last_error_.clear();status_->setText(text("model.analyzing"));refreshActions();tracks_->setCurrentIndex(3);
        }catch(...){failure_=std::current_exception();last_error_=model_->failureText(failure_,"model.error.configuration");status_->setText(last_error_);refreshActions();}
    }
    void recoverCurrent() {
        if(!current_result_ || busy_ || saving_ || save_dialog_)return;recovery_->setSelection(views_[0]->range(),channel_->range());
        try {auto selected=recovery_->draft();if(!selected){recovery_->setResult({},{});auto copy=std::make_shared<RecordedWaveformCompletion>(*current_result_);copy->recovery.reset();copy->recovery_request.reset();current_result_=std::move(copy);refreshProvenance();refreshActions();return;}
            selected->event=loaded_event_;capture_->pauseFollowing();pending_window_=pending_window_ || window_refresh_.isActive();window_refresh_.stop();restoring_.reset();pending_reveal_.reset();RecordedWaveformRequest request;request.completed=current_result_;request.recovery=std::move(selected);generation_=worker_.request(std::move(request));busy_=true;last_error_.clear();status_->setText(text("recovery.analyzing"));refreshActions();tracks_->setCurrentIndex(4);
        }catch(...){failure_=std::current_exception();last_error_=recovery_->failureText(failure_,"recovery.error.configuration");status_->setText(last_error_);refreshActions();}
    }
    void locateRecovery(const std::vector<SignalSampleRange>& ranges,std::shared_ptr<const WaveformAnalysis> input,bool original) {
        if(busy_ || saving_ || !input || ranges.empty() || !recovery_->result() || recovery_->result()->analysis!=input)return;
        const auto first=ranges.front().first,last=ranges.back().first+ranges.back().count-1;
        if(original && input->processed){revealOriginal(input->source_position(first));return;}
        a_=input->source_position(first).sample;b_=input->source_position(last).sample;
        if(input->processed){channel_->selectAnalysisRange({first,last-first+1});refreshCursors(false);tracks_->setCurrentIndex(1);}else {changeViewRange({first,last-first+1});refreshCursors();tracks_->setCurrentIndex(0);}queueWindowRefresh();
    }
    void refreshProvenance() {
        if(!analysis_)return;const auto& source=*analysis_->source;
        const Json detail={{"source_kind",recording_?"recording":"memory-capture"},{"source_id",source.source_id},{"source_first_sample",std::to_string(source.first_sample())},{"source_end_sample",std::to_string(source.end_sample())},
            {"source_metadata",recording_?recording_->storage.metadata:Json::object()},{"source_identity",{{"metadata_sha256",identity_.metadata_sha256},{"normalized_window_sha256",identity_.normalized_window_sha256},{"scope","selected-normalized-window-and-integer-axis"}}},
            {"analysis_method",analysis_->method},{"channel",channel_->snapshot()},{"channel_source_identity",current_result_?Json{{"metadata_sha256",current_result_->channel_source_identity.metadata_sha256},{"normalized_source_sha256",current_result_->channel_source_identity.normalized_source_sha256}}:Json()},
            {"window",window_->snapshot()},{"model",model_->snapshot()},{"recovery",recovery_->snapshot()},{"processing",source.processing},{"event",loaded_event_}};
        provenance_->setPlainText(QString::fromStdString(detail.dump(2)));
    }
    TraceViewSeries series(const char* id,const QColor& color,std::optional<double> wrap={},bool dashed=false) const {
        const auto& trace=analysis_->trace(id);return {std::shared_ptr<const AnalysisTrace>(analysis_,&trace),color,text(trace.label_key),wrap,dashed};
    }
    void refreshTraces(bool preserve) {
        if(!analysis_)return;const auto range=views_[0]->range();QScopedValueRollback guard(synchronizing_,true);
        const auto dark=palette().base().color().lightness()<128;const QColor primary=dark?QColor("#65d9c5"):QColor("#087f78"),secondary=dark?QColor("#efb766"):QColor("#a86b13"),phaseColor=dark?QColor("#b8a0ee"):QColor("#7854b8");
        overview_->setAnalysis(analysis_,{series("amplitude",primary)});
        views_[0]->setAnalysis(analysis_,{series("i",primary),series("q",secondary,{},true)});views_[1]->setAnalysis(analysis_,{series("amplitude",primary)});
        plane_->setAnalysis(analysis_,std::shared_ptr<const AnalysisTrace>(analysis_,&analysis_->trace("i")),std::shared_ptr<const AnalysisTrace>(analysis_,&analysis_->trace("q")));
        const auto phase=phase_->currentData().toString().toStdString();views_[2]->setAnalysis(analysis_,{series(phase.c_str(),phaseColor,phase=="phase"?std::optional<double>(2*std::numbers::pi):std::nullopt)});
        std::vector<TraceViewSeries> frequency{series("frequency",primary)};if(smooth_->isChecked())frequency.push_back(series("smoothed_frequency",secondary,{},true));views_[3]->setAnalysis(analysis_,std::move(frequency));
        if(preserve && range.count)for(auto* view:views_)view->setRange(range);
        plane_->setRange(views_[0]->range());plane_->setCursors(a_,b_);
        for(auto* view:views_)view->setCursors(a_,b_);
        const auto current=views_[0]->range();overview_->setCursors(current.first,current.first+current.count-1);
        plane_->setRange(current);
    }
    void changeViewRange(SignalSampleRange range,bool linkChannel=true) {
        if(synchronizing_ || !analysis_ || !range.count)return;
        if(linkChannel && current_result_ && current_result_->channel_file_range &&
            (range.first<analysis_->source->first_sample() || range.first>analysis_->source->end_sample() || range.count>analysis_->source->end_sample()-range.first)) {
            channel_->setSourceRange(range);queueWindowRefresh();status_->setText(text("channel.outside_preview"));return;
        }
        QScopedValueRollback guard(synchronizing_,true);
        for(auto* view:views_)view->setRange(range);
        const auto current=views_[0]->range();overview_->setCursors(current.first,current.first+current.count-1);
        plane_->setRange(current);
        viewRange_->setText(text("view.range",{{"count",current.count},{"duration",number(1000.*current.count/analysis_->source->stream.sample_rate,3)}}));
        if(linkChannel)channel_->setSourceRange(current);
        model_->setSelection(current,channel_->range());
        recovery_->setSelection(current,channel_->range());
        int preset=-1;const auto& source=*analysis_->source;
        queueWindowRefresh();
        if(current.first==source.first_sample() && current.count==source.end_sample()-source.first_sample())preset=0;
        else for(int n=1;n<scale_->count();++n)if(current.count==static_cast<std::uint64_t>(std::max(1LL,std::llround(scale_->itemData(n).toDouble()*source.stream.sample_rate)))){preset=n;break;}
        scale_->setCurrentIndex(preset);
    }
    void queueWindowRefresh() {
        if(const auto result=window_->result()) {
            const auto range=window_->processed()?channel_->range():views_[0]->range();
            if(result->range!=range)window_refresh_.start();else window_refresh_.stop();
        }else window_refresh_.stop();
        refreshActions();
    }
    void setScale() {
        if(!analysis_)return;const auto seconds=scale_->currentData().toDouble();
        if(seconds==0){views_[0]->resetRange();return;}
        const auto source=analysis_->source;const auto length=std::clamp<std::uint64_t>(std::llround(seconds*source->stream.sample_rate),1,source->end_sample()-source->first_sample());
        const auto center=std::clamp(b_.value_or(source->first_sample()),source->first_sample(),source->end_sample()-1);const auto first=center-std::min(center-source->first_sample(),length/2);
        changeViewRange({std::min(first,source->end_sample()-length),length});
    }
    void refreshCursors(bool linkChannel=true) {
        if(!analysis_ || !a_ || !b_)return;for(auto* view:views_)view->setCursors(a_,b_);
        plane_->setCursors(a_,b_);
        if(linkChannel)channel_->setSourceCursors(a_,b_);
        model_->setSourceCursors(a_,b_);
        const auto rate=analysis_->source->stream.sample_rate;const auto origin=recording_?analysis_->source->recording_origin:analysis_->source->first_sample();
        const auto time=[&](auto sample){return 1000.*(sample>=origin?double(sample-origin):-double(origin-sample))/rate;};
        readouts_[0]->setText(quantity(time(*a_),"ms",6));readouts_[1]->setText(quantity(time(*b_),"ms",6));
        const auto difference=*b_>=*a_?double(*b_-*a_):-double(*a_-*b_);readouts_[2]->setText(quantity(difference/rate*1000,"ms",6));
        readouts_[3]->setText((*b_>=*a_?QString():QStringLiteral("−"))+QString::number(*b_>=*a_?*b_-*a_:*a_-*b_));
        const auto ai=analysis_->value_index(*a_),bi=analysis_->value_index(*b_);const auto value=[&](const char* id,std::optional<std::size_t> at){return at?analysis_->trace(id).values[*at]:std::numeric_limits<double>::quiet_NaN();};
        readouts_[4]->setText(quantity((value("frequency",bi)-value("frequency",ai))/1000,"kHz",4));
        bool continuous=ai && bi && std::isfinite(value("phase",ai)) && std::isfinite(value("phase",bi));
        if(continuous){const auto& frequency=analysis_->trace("frequency").values;continuous=std::all_of(frequency.begin()+std::min(*ai,*bi)+1,frequency.begin()+std::max(*ai,*bi)+1,[](double f){return std::isfinite(f);});}
        readouts_[5]->setText(continuous?quantity(value("unwrapped_phase",bi)-value("unwrapped_phase",ai),"rad",4):text("unavailable"));
        const auto file_position=recording_?recording_->stored_index(*b_):std::optional<std::uint64_t>();
        readouts_[6]->setText(QString::number(*b_));readouts_[7]->setText(file_position?QString::number(*file_position):text("unavailable"));
        readouts_[8]->setText(quantity(value("i",bi),"",6));readouts_[9]->setText(quantity(value("q",bi),"",6));readouts_[10]->setText(quantity(value("amplitude",bi),"",6));
        readouts_[11]->setText(quantity(value("phase",bi),"rad",4));readouts_[12]->setText(quantity(value("unwrapped_phase",bi),"rad",4));readouts_[13]->setText(quantity(value("phase_difference",bi),"rad",6));readouts_[14]->setText(quantity(value("frequency",bi)/1000,"kHz",4));
        validity_->setText(!bi?text(recording_ && recording_->stored_index(*b_)?"sample.not_loaded":"sample.missing"):std::isfinite(value("frequency",bi))?text("sample.valid"):text("sample.derived_invalid"));
    }
    Json configuration() const {
        if(!analysis_)throw std::runtime_error("No waveform to save");const auto range=views_[0]->range();const auto settings=applied_options_;
        Json source=recording_?Json{{"metadata_path",path_to_utf8(analysis_->source->recording_path)},{"first_file_sample",std::to_string(file_range_.first)},{"sample_count",std::to_string(file_range_.count)},{"metadata_sha256",identity_.metadata_sha256},{"normalized_window_sha256",identity_.normalized_window_sha256}}:
            Json{{"kind","memory-capture"},{"source_id",analysis_->source->source_id},{"first_source_sample",std::to_string(analysis_->source->first_sample())},{"sample_count",std::to_string(analysis_->source->sample_count())},{"available_until_close",true},{"normalized_window_sha256",identity_.normalized_window_sha256}};
        auto channel=channel_->configuration();const auto window=window_->configuration(),model=model_->configuration(),recovery=recovery_->configuration();
        if(channel.at("version")==2 && current_result_)channel["input"]["identity"]={{"format","org.amc.normalized-recorded-range"},{"version",1},
            {"metadata_sha256",current_result_->channel_source_identity.metadata_sha256},{"normalized_source_sha256",current_result_->channel_source_identity.normalized_source_sha256}};
        return {{"format","org.amc.waveform-analysis"},{"version",recovery.at("enabled").get<bool>()?(recovery.at("version")==2?8:7):model.at("enabled").get<bool>()?6:channel.at("version")==2?5:window.at("enabled").get<bool>()?4:channel.at("enabled").get<bool>()?3:1},{"source",std::move(source)},{"channel",channel},{"window",window},{"model",model},{"recovery",recovery},{"track_page",tracks_->currentIndex()},{"processed_tab",tracks_->currentIndex()==1},
            {"parameters",{{"minimum_amplitude",settings.minimum_amplitude},{"relative_amplitude_floor",settings.relative_amplitude_floor},{"frequency_smoothing_samples",settings.frequency_smoothing_samples}}},
            {"view",{{"first",std::to_string(range.first)},{"count",std::to_string(range.count)},{"cursor_a",std::to_string(a_.value_or(range.first))},{"cursor_b",std::to_string(b_.value_or(range.first))},{"wrapped_phase",phase_->currentIndex()==1},{"smoothed_frequency",smooth_->isChecked()}}},{"event",loaded_event_}};
    }
    void saveAnalysis() {
        if(!analysis_ || busy_ || saving_)return;
        capture_->pauseFollowing();save_dialog_=true;const auto savedAnalysis=analysis_;const auto savedRecording=recording_;const auto savedIdentity=identity_;auto config=configuration();
        QFileDialog dialog(this,text(savedRecording?"save.analysis":"save.capture_analysis"));dialog.setAcceptMode(QFileDialog::AcceptSave);dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setNameFilter(text("filter.analysis"));dialog.setDefaultSuffix("amc-analysis.json");dialog.selectFile("analysis.amc-analysis.json");
        const auto accepted=dialog.exec()==QDialog::Accepted && !dialog.selectedFiles().isEmpty();save_dialog_=false;
        if(!accepted){if(pending_capture_){auto source=std::exchange(pending_capture_,{});analyzeCapture(std::move(source));}return;}
        try {
            WaveformSaveRequest request;request.configuration_path=QFileInfo(dialog.selectedFiles().front()).filesystemFilePath();request.configuration=std::move(config);
            if(savedRecording)request.protected_paths={savedAnalysis->source->recording_path,savedRecording->storage.data_path};
            else {request.capture=savedAnalysis->source;request.capture_identity=savedIdentity;}
            save_generation_=save_worker_.request(std::move(request));saving_=true;last_save_={{"committed",false}};last_error_.clear();status_->setText(text("save.saving"));refreshActions();
        }catch(...){last_error_=text("error.save");status_->setText(last_error_);}
    }
    void exportRecovery(bool binary) {
        if(!analysis_ || busy_ || saving_ || save_dialog_ || !recovery_->result())return;
        capture_->pauseFollowing();const auto evidence=recovery_->result();const auto selected=recovery_->selectedBytes();
        if(binary && !selected){last_error_=text("recovery.export.unknown");status_->setText(last_error_);return;}
        auto config=configuration();const auto savedRecording=recording_;const auto savedAnalysis=analysis_;save_dialog_=true;
        QFileDialog dialog(this,text(binary?"recovery.export_bytes":"recovery.export_json"));dialog.setAcceptMode(QFileDialog::AcceptSave);dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setNameFilter(text(binary?"recovery.export.bytes_filter":"recovery.export.json_filter"));dialog.setDefaultSuffix(binary?"bin":"json");dialog.selectFile(binary?"recovered-selection.bin":"recovery-evidence.json");
        const auto accepted=dialog.exec()==QDialog::Accepted && !dialog.selectedFiles().isEmpty();save_dialog_=false;
        if(!accepted){if(pending_capture_){auto source=std::exchange(pending_capture_,{});analyzeCapture(std::move(source));}return;}
        try {
            WaveformSaveRequest request;request.purpose=binary?WaveformFilePurpose::recovery_bytes:WaveformFilePurpose::recovery_json;
            request.configuration_path=QFileInfo(dialog.selectedFiles().front()).filesystemFilePath();request.configuration=std::move(config);request.recovery=evidence;request.selection=selected;
            if(savedRecording)request.protected_paths={savedAnalysis->source->recording_path,savedRecording->storage.data_path};
            save_generation_=save_worker_.request(std::move(request));saving_=true;last_export_={{"committed",false}};last_error_.clear();status_->setText(text("recovery.export.saving"));refreshActions();
        }catch(...){last_error_=text("recovery.export.error");status_->setText(last_error_);}
    }
    void refreshActions() {
        const auto writing=saving_ || save_worker_.busy();open_recording_->setEnabled(!writing);open_analysis_->setEnabled(!writing);
        applyRange_->setEnabled(bool(recording_) && !writing);apply_options_->setEnabled(bool(analysis_) && !writing);
        channel_->setAvailable(bool(analysis_) && !busy_ && !writing);
        window_->setAvailable(bool(analysis_) && !busy_ && !writing);
        model_->setAvailable(bool(analysis_) && !busy_ && !writing);
        recovery_->setAvailable(bool(analysis_) && !busy_ && !writing);
        save_->setEnabled(bool(analysis_) && !busy_ && !writing && !window_refresh_.isActive() && !pending_window_);save_->setText(text(recording_?"save.analysis":"save.capture_analysis"));
        save_->setToolTip(text(recording_?"save.reference_hint":"save.capture_hint"));cancel_->setEnabled(busy_ || writing || window_refresh_.isActive() || pending_window_);
    }
    void pollSave() {
        const auto done=save_worker_.takeLatest();if(!done || done->generation!=save_generation_)return;
        saving_=false;if(done->purpose!=WaveformFilePurpose::analysis) {
            last_export_={{"path",path_to_utf8(done->configuration_path)},{"binary",done->purpose==WaveformFilePurpose::recovery_bytes},{"committed",done->export_committed},{"canceled",done->canceled},{"error_key",done->error_key}};
            if(done->export_committed)status_->setText(text("recovery.export.saved",{{"path",path_to_utf8(done->configuration_path)}}));
            else if(done->canceled)status_->setText(text("canceled.keep"));
            else {last_error_=text(done->error_key);status_->setText(last_error_);}
        }else {
        last_save_={{"configuration_path",path_to_utf8(done->configuration_path)},{"capture_metadata",path_to_utf8(done->capture_metadata)},
            {"committed",done->configuration_committed},{"verified_capture",done->verified_capture},{"canceled",done->canceled},{"error_key",done->error_key}};
        if(done->configuration_committed)status_->setText(text("save.complete",{{"path",path_to_utf8(done->configuration_path)}}));
        else if(done->canceled)status_->setText(text(done->verified_capture?"save.canceled_with_capture":"canceled.keep",{{"path",path_to_utf8(done->capture_metadata)}}));
        else {last_error_=text(done->verified_capture?"save.error.with_capture":done->error_key,{{"path",path_to_utf8(done->capture_metadata)}});status_->setText(last_error_);}
        }
        refreshActions();
        if(pending_capture_){auto source=std::exchange(pending_capture_,{});analyzeCapture(std::move(source));}
        else if(pending_window_){pending_window_=false;window_refresh_.start();refreshActions();}
    }
    void chooseAnalysis() {
        const auto path=QFileDialog::getOpenFileName(this,text("open.analysis"),{},text("filter.analysis"));if(path.isEmpty())return;
        try {
            const auto native=QFileInfo(path).filesystemFilePath();if(std::filesystem::file_size(native)>8*1024*1024)throw std::length_error("Analysis configuration exceeds its budget");
            std::ifstream file(native);Json config;file>>config;
            if(config.at("format")!="org.amc.waveform-analysis" || (config.at("version")!=1 && config.at("version")!=2 && config.at("version")!=3 && config.at("version")!=4 && config.at("version")!=5 && config.at("version")!=6 && config.at("version")!=7 && config.at("version")!=8))throw std::invalid_argument("Unsupported analysis configuration");
            if(config.at("version")<3 && config.contains("channel") && config.at("channel").value("enabled",false))throw std::invalid_argument("Processed analysis requires its declared protocol version");
            if(config.at("version")<4 && config.contains("window") && config.at("window").value("enabled",false))throw std::invalid_argument("Window research requires its declared protocol version");
            if(config.at("version")<5 && config.contains("channel") && config.at("channel").value("version",1)==2)throw std::invalid_argument("Recorded channel inputs require their declared protocol version");
            if(config.at("version")<6 && config.contains("model") && config.at("model").value("enabled",false))throw std::invalid_argument("Model comparisons require their declared protocol version");
            if(config.at("version")<8 && config.contains("recovery") && config.at("recovery").value("enabled",false) && config.at("recovery").value("version",1)==2)throw std::invalid_argument("Complex recovery requires analysis v8");
            if(config.at("version")<7 && config.contains("recovery") && config.at("recovery").value("enabled",false))throw std::invalid_argument("Recovery requires its declared protocol version");
            channel_->restoreDraft(config.value("channel",Json()));
            window_->restoreDraft(config.value("window",Json()));
            model_->restoreDraft(config.value("model",Json()));
            recovery_->restoreDraft(config.value("recovery",Json()));
            const auto& source=config.at("source");const auto& parameters=config.at("parameters");
            const auto relative=parameters.at("relative_amplitude_floor").get<double>();const auto smoothing=parameters.at("frequency_smoothing_samples").get<unsigned>();
            if(!std::isfinite(relative) || relative<0 || relative>1 || !smoothing || smoothing>100001 || parameters.at("minimum_amplitude").get<double>()!=WaveformOptions{}.minimum_amplitude)throw std::invalid_argument("Invalid saved analysis parameters");
            floor_->setValue(relative);if(floor_->value()!=relative)throw std::invalid_argument("Saved estimator precision is not supported");
            averaging_->setValue(static_cast<int>(smoothing));event_=config.value("event",Json::object());restoring_=config;
            const auto count=sampleIndex(source.at("sample_count")),first=sampleIndex(source.at("first_file_sample"));
            const auto validDigest=[](const std::string& digest){return digest.size()==64 && std::all_of(digest.begin(),digest.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});};
            if(!validDigest(source.at("metadata_sha256").get<std::string>()) || !validDigest(source.at("normalized_window_sha256").get<std::string>()))throw std::invalid_argument("Missing or invalid source identity");
            auto sourcePath=path_from_utf8(source.at("metadata_path").get<std::string>());
            if(config.at("version")!=1) {
                const auto base=source.value("path_base",std::string("absolute"));
                if(base=="analysis-directory"){if(sourcePath.is_absolute())throw std::invalid_argument("Relative analysis source must not be absolute");sourcePath=native.parent_path()/sourcePath;}
                else if(base!="absolute")throw std::invalid_argument("Unsupported analysis path reference");
            }
            open(sourcePath,SignalSampleRange{first,count},
                {source.at("metadata_sha256").get<std::string>(),source.at("normalized_window_sha256").get<std::string>()},true);
        }catch(...){restoring_.reset();last_error_=text("error.configuration");status_->setText(last_error_);}
    }
    void restoreView(const Json& config) {
        try {const auto& view=config.at("view");phase_->setCurrentIndex(view.value("wrapped_phase",false)?1:0);smooth_->setChecked(view.value("smoothed_frequency",false));
            const auto& source=*analysis_->source;const auto a=sampleIndex(view.at("cursor_a")),b=sampleIndex(view.at("cursor_b"));
            const auto first=config.at("version")>=5 && recording_?recording_->segments.front().first_source_sample:source.first_sample();
            const auto last=config.at("version")>=5 && recording_?recording_->segments.back().first_source_sample+recording_->segments.back().count-1:source.end_sample()-1;
            a_=std::clamp(a,first,last);b_=std::clamp(b,first,last);
            changeViewRange({sampleIndex(view.at("first")),sampleIndex(view.at("count"))},false);refreshCursors();
            channel_->restoreView(config.value("channel",Json::object()));window_->restoreView(config.value("window",Json::object()));model_->restoreView(config.value("model",Json::object()));recovery_->restoreView(config.value("recovery",Json::object()));tracks_->setCurrentIndex(config.value("track_page",config.value("processed_tab",false)?1:0));
            window_refresh_.stop();pending_window_=false;refreshActions();
        }catch(...){status_->setText(text("error.view_restored"));}
    }
    Json snapshot() const {
        Json result={{"state",saving_?"saving":busy_?"analyzing":!last_error_.isEmpty()?"failed":analysis_?"ready":"empty"},{"protocol_version",3},{"pending_generation",generation_},{"capture",capture_->snapshot()},{"last_save",last_save_},{"last_export",last_export_}};
        if(analysis_){result["configuration"]=configuration();result["loaded_samples"]=analysis_->source->sample_count();result["trace_payload_bytes"]=analysis_->payload_bytes();result["channel"]=channel_->snapshot();result["window"]=window_->snapshot();result["model"]=model_->snapshot();result["recovery"]=recovery_->snapshot();}
        return result;
    }
    QString language_;TextCatalog catalog_;RecordedWaveformWorker worker_;WaveformAnalysisSaveWorker save_worker_;QTimer timer_;
    std::uint64_t save_generation_=0;bool saving_=false,save_dialog_=false;Json last_save_=Json::object(),last_export_=Json::object();std::shared_ptr<const SignalWindow> pending_capture_;
    std::shared_ptr<const WaveformAnalysis> analysis_;std::shared_ptr<const SignalSourceInfo> recording_;WaveformSourceIdentity identity_;SignalSampleRange file_range_;
    std::uint64_t generation_=0;bool busy_=false,synchronizing_=false;std::exception_ptr failure_;QString last_error_;Json event_=Json::object();std::optional<Json> restoring_;
    WaveformOptions applied_options_;
    std::optional<std::uint64_t> a_,b_;
    Json loaded_event_=Json::object();
    QPointer<QWidget> inspector_;QWidget* fileRange_=nullptr;QLabel *source_=nullptr,*viewRange_=nullptr,*status_=nullptr,*validity_=nullptr;QPlainTextEdit* provenance_=nullptr;
    QLineEdit *first_=nullptr,*count_=nullptr;QPushButton *applyRange_=nullptr,*apply_options_=nullptr;QAction *save_=nullptr,*cancel_=nullptr,*open_recording_=nullptr,*open_analysis_=nullptr;QComboBox *phase_=nullptr,*scale_=nullptr;
    QCheckBox* smooth_=nullptr;QDoubleSpinBox* floor_=nullptr;QSpinBox* averaging_=nullptr;TraceView* overview_=nullptr;TracePlaneView* plane_=nullptr;WaveformCaptureControls* capture_=nullptr;
    std::array<TraceView*,4> views_{};std::array<QLabel*,15> readouts_{};
    QTabWidget* tracks_=nullptr;WaveformChannelPanel* channel_=nullptr;WaveformWindowPanel* window_=nullptr;WaveformModelPanel* model_=nullptr;WaveformRecoveryPanel* recovery_=nullptr;QTimer window_refresh_;bool pending_window_=false;
    std::shared_ptr<const RecordedWaveformCompletion> current_result_;
    struct OriginalReveal {std::uint64_t sample;std::optional<std::uint64_t> a;Json channel,window,model,recovery;};std::optional<OriginalReveal> pending_reveal_;
};
}
LiveResearchProvider waveformLaboratoryProvider(){return {"waveform-laboratory",[](const QString& language,QWidget* parent){return (new Laboratory(language,parent))->tool();},3};}
}
