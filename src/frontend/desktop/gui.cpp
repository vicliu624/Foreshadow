#include "signalscope/backend/sessions/monitor.hpp"
#include "signalscope/backend/sessions/reception.hpp"
#include "signalscope/backend/sessions/replay_session.hpp"
#include "analysis_panel.hpp"
#include "desktop_language.hpp"
#include "legal_notices.hpp"
#include "desktop_theme.hpp"
#include "desktop_error.hpp"
#include "desktop_tools.hpp"
#include "signalscope/desktop/number_format.hpp"
#include "receiver_dialog.hpp"
#include "receiver_probe.hpp"
#include "search_width_control.hpp"
#include "signalscope/backend/sessions/snapshot_analysis.hpp"
#include "plugins/desktop/display/signal_display.hpp"
#include "desktop_menus.hpp"
#include "live_tools.hpp"
#include "event_presentation.hpp"
#include "status_presentation.hpp"
#include "desktop_recording_export.hpp"
#include "toolbox_center.hpp"
#include "workspace.hpp"
#include "runtime_status.hpp"
#include "signalscope/backend/sessions/process_metrics.hpp"
#include <QDockWidget>
#include <QScrollArea>
#include <QGridLayout>
#include "toolbox_services.hpp"
#include "plugins/desktop/research/external_research.hpp"
#include "plugins/desktop/research/research_range.hpp"
#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

using namespace signalscope;
namespace {
struct Shared {
    std::mutex mutex;SpectrumFrame spectrum;Json status=Json::object();std::exception_ptr error;bool done=false;
};
class Window:public QMainWindow {
public:
    Window(const QString& output,const QString& input,desktop::ReceiverSelection selection,
           const QString& pluginDirectory,const QString& language,std::shared_ptr<distribution::Service> updates)
        :output_(output),input_(input),selection_(std::move(selection)),receiver_(&receiver_by_name(selection_.backend.toStdString())) {
        setWindowTitle(QCoreApplication::translate("MonitorWindow", "Foreshadow · Signal monitoring and analysis"));resize(1120,680);setMinimumSize(640,480);
        QSettings settings;
        detection_workers_=std::clamp(settings.value("receiver/detection_workers",int(recommendedAnalysisWorkers())).toInt(),1,32);
        threshold_db_=settings.value("receiver/threshold_db",18).toDouble();
        if(!std::isfinite(threshold_db_)||threshold_db_<6||threshold_db_>60)threshold_db_=18;
        workspace_=new desktop::Workspace(*this);
        auto* center=new QWidget;auto* layout=new QVBoxLayout(center);
        receiver_settings_=new QPushButton(QCoreApplication::translate("ReceiverDialog","Receiver settings"));
        layout->addWidget(receiver_settings_);
        auto* tuning=new QWidget;
        auto* controls=new QFormLayout(tuning);
        controls->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        controls->setContentsMargins(0,0,0,0);
        frequency_=new QDoubleSpinBox;frequency_->setDecimals(6);frequency_->setSuffix(" MHz");
        search_width_=new desktop::SearchWidthControl;
        gain_=new QComboBox;
        gain_label_=new QLabel;
        loadReceiverPreferences();
        connect(receiver_settings_,&QPushButton::clicked,this,[this]{chooseReceiver();});
        controls->addRow(QCoreApplication::translate("MonitorWindow", "Center frequency"),frequency_);
        controls->addRow(gain_label_,gain_);
        controls->addRow(QCoreApplication::translate("MonitorWindow", "Search width"),search_width_);
        layout->addWidget(tuning);
        start_=new QPushButton(QCoreApplication::translate("MonitorWindow", "Start listening"));stop_=new QPushButton(QCoreApplication::translate("MonitorWindow", "Stop"));stop_->setEnabled(false);
        pause_=new QPushButton(QCoreApplication::translate("MonitorWindow", "Pause replay"));pause_->setEnabled(false);
        auto* transport=new QWidget;
        auto* transportLayout=new QGridLayout(transport);
        transportLayout->setContentsMargins(0,0,0,0);
        transportLayout->addWidget(start_,0,0);transportLayout->addWidget(stop_,0,1);transportLayout->addWidget(pause_,1,0,1,2);
        layout->addWidget(transport);
        auto* recordingActions=new QWidget;
        auto* paths=new QGridLayout(recordingActions);
        paths->setContentsMargins(0,0,0,0);
        auto* live=new QPushButton(QCoreApplication::translate("MonitorWindow", "Live input"));auto* open=new QPushButton(QCoreApplication::translate("MonitorWindow", "Open I/Q"));auto* refresh=new QPushButton(QCoreApplication::translate("MonitorWindow", "Refresh list"));
        export_file_=new QPushButton(QCoreApplication::translate("MonitorWindow", "Export"));
        auto* analyze_file=new QPushButton(QCoreApplication::translate("MonitorWindow", "Analyze"));
        quota_=new QSpinBox;quota_->setRange(64,16384);quota_->setValue(1024);quota_->setSuffix(" MiB");
        limit_=new QDoubleSpinBox;limit_->setRange(1,60);limit_->setValue(QSettings().value("receiver/max_event_seconds",8).toDouble());limit_->setSuffix(QCoreApplication::translate("MonitorWindow", " s"));
        paths->addWidget(live,0,0);paths->addWidget(open,0,1);paths->addWidget(refresh,1,0);paths->addWidget(export_file_,1,1);paths->addWidget(analyze_file,2,0,1,2);
        auto* retention=new QWidget;
        auto* retentionLayout=new QFormLayout(retention);
        retentionLayout->setContentsMargins(0,0,0,0);
        retentionLayout->addRow(QCoreApplication::translate("MonitorWindow", "Clip limit"),limit_);
        retentionLayout->addRow(QCoreApplication::translate("MonitorWindow", "Quota"),quota_);
        layout->addWidget(recordingActions);layout->addWidget(retention);layout->addStretch();
        auto* controlsScroll=new QScrollArea;controlsScroll->setWidgetResizable(true);controlsScroll->setFrameShape(QFrame::NoFrame);controlsScroll->setWidget(center);
        auto* statusPane=new QWidget;auto* statusLayout=new QVBoxLayout(statusPane);

        source_=new QLabel;
        source_->setTextFormat(Qt::PlainText);
        source_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        source_->setWordWrap(true);statusLayout->addWidget(source_);updateSource();
        coverage_=new QLabel;coverage_->setWordWrap(true);statusLayout->addWidget(coverage_);updateCoverage();
        configuration_feedback_=new QLabel;configuration_feedback_->setWordWrap(true);
        configuration_feedback_->setObjectName("receiver-configuration-feedback");statusLayout->addWidget(configuration_feedback_);
        connect(frequency_,&QDoubleSpinBox::valueChanged,this,[this](double){updateCoverage();});
        connect(search_width_,&desktop::SearchWidthControl::valueChanged,this,[this](int){updateCoverage();});
        state_=new QLabel(QCoreApplication::translate("MonitorWindow", "Idle"));state_->setWordWrap(true);statusLayout->addWidget(state_);
        power_=new QLabel(QCoreApplication::translate("MonitorWindow", "Full-band level — dBFS · Uncalibrated RF power"));power_->setWordWrap(true);statusLayout->addWidget(power_);
        displays_=desktop::createSignalDisplays();
        evidence_=new QTextEdit;evidence_->setReadOnly(true);evidence_->setPlainText(QCoreApplication::translate("MonitorWindow", "Select a detected event to inspect its measurements and raw evidence."));
        files_=new QTableWidget(0,4);files_->setHorizontalHeaderLabels({QCoreApplication::translate("MonitorWindow", "Recording"),QCoreApplication::translate("MonitorWindow", "Duration (s)"),QCoreApplication::translate("MonitorWindow", "Status"),QCoreApplication::translate("MonitorWindow", "Center (MHz)")});
        files_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);files_->setSelectionBehavior(QAbstractItemView::SelectRows);files_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        analysis_=new AnalysisPanel(pluginDirectory,language);
        const auto eventColumns=desktop::eventColumns();
        events_=new QTableWidget(0,eventColumns.size());events_->setHorizontalHeaderLabels(eventColumns);
        events_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);events_->setSelectionBehavior(QAbstractItemView::SelectRows);events_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(int i=0;i<eventColumns.size();++i)events_->horizontalHeaderItem(i)->setToolTip(eventColumns[i]);
        storage_=new QLabel(QCoreApplication::translate("MonitorWindow", "Pre-trigger I/Q stays in memory. Confirmed events are saved up to the configured limit."));storage_->setWordWrap(true);statusLayout->addWidget(storage_);statusLayout->addStretch();
        storage_->setToolTip(QCoreApplication::translate("MonitorWindow", "The oldest automatic recordings expire when storage limits are reached. Export clips to keep them."));
        workspace_->add("session",QCoreApplication::translate("Workspace","Receiver and session"),controlsScroll,Qt::LeftDockWidgetArea);
        workspace_->add("status",QCoreApplication::translate("Workspace","Status and health"),statusPane,Qt::LeftDockWidgetArea);
        for(auto* display:displays_)workspace_->add("display/"+display->objectName(),display->windowTitle(),display,Qt::RightDockWidgetArea);
        workspace_->add("events",QCoreApplication::translate("MonitorWindow","Detected events"),events_,Qt::BottomDockWidgetArea);
        workspace_->add("evidence",QCoreApplication::translate("MonitorWindow","Detection evidence"),evidence_,Qt::RightDockWidgetArea);
        workspace_->add("recordings",QCoreApplication::translate("MonitorWindow","Recordings and replay"),files_,Qt::RightDockWidgetArea);
        workspace_->add("analysis",QCoreApplication::translate("MonitorWindow","Signal analysis"),analysis_,Qt::RightDockWidgetArea);
        live_tools_=std::make_unique<desktop::LiveTools>(desktop::liveResearchProviders(),language,this);
        for(const auto& panel:live_tools_->panels()) {
            workspace_->add(panel.id,panel.title,panel.widget,panel.preferredArea);
            if(panel.relativeTo)workspace_->place(panel.widget,panel.relativeTo,panel.orientation);
            workspace_->setVisible(panel.widget,panel.initiallyVisible);
        }
        live_tools_->showPanel=[this](QWidget* widget){workspace_->show(widget);};
        live_tools_->setReceptionAcquire([this](const ReceptionDemand& demand,ReceptionConsumer consumer){
            if(capabilityQuery_.state()!=QProcess::NotRunning)throw std::runtime_error("receiver_probe_busy");
            auto request=demand;auto configured=receiverConfiguration();request.preferences=configured;
            if(request.configured_receiver){if(!std::isfinite(request.frequency_hz) || request.frequency_hz<0 || request.frequency_hz>=std::ldexp(1.,64))throw std::invalid_argument("reception_frequency_invalid");configured.frequency=static_cast<std::uint64_t>(std::round(request.frequency_hz));configured.required_passband_hz=request.passband_hz;request.exact=configured;}
            return receiverSession(configured)->acquire(std::move(request),std::move(consumer));
        },[this]{chooseReceiver();});
        controlsPane_=controlsScroll;statusPane_=statusPane;
        for(const auto& layout:live_tools_->layouts())workspace_->addLayout(layout.id,layout.title,[this,layout]{researchWorkspace(layout);});
        runtimeStatus_=new desktop::RuntimeStatus(language,this);statusBar()->addPermanentWidget(runtimeStatus_,1);
        connect(runtimeStatus_->device,&QToolButton::clicked,this,[this]{workspace_->show(controlsPane_);});
        connect(runtimeStatus_->tasks,&QToolButton::clicked,this,[this]{
            QMenu menu;for(const auto& panel:live_tools_->panels())menu.addAction(panel.title,this,[this,widget=panel.widget]{workspace_->show(widget);});
            menu.addSeparator();menu.addAction(runtimeStatus_->text("disconnect"),this,[this]{disconnectReceiver();});
            menu.exec(runtimeStatus_->tasks->mapToGlobal(QPoint(0,0)));
        });
        connect(runtimeStatus_->load,&QToolButton::clicked,this,[this]{workspace_->show(statusPane_);});
        connect(runtimeStatus_->health,&QToolButton::clicked,this,[this]{if(!live_tools_->showHealth())workspace_->show(statusPane_);});
        auto* view=findChild<QMenu*>("workspace-view-menu");auto* statusToggle=view->addAction(runtimeStatus_->text("view"));statusToggle->setObjectName("view/status-bar");statusToggle->setCheckable(true);
        statusToggle->setChecked(settings.value("desktop/status_bar_visible",true).toBool());statusBar()->setVisible(statusToggle->isChecked());
        connect(statusToggle,&QAction::toggled,this,[this](bool show){statusBar()->setVisible(show);QSettings{}.setValue("desktop/status_bar_visible",show);});
        connect(start_,&QPushButton::clicked,this,[this]{start();});
        connect(stop_,&QPushButton::clicked,this,[this]{requestStop();});
        connect(pause_,&QPushButton::clicked,this,[this]{paused_=!paused_;pause_->setText(paused_?QCoreApplication::translate("MonitorWindow", "Resume replay"):QCoreApplication::translate("MonitorWindow", "Pause replay"));});
        connect(live,&QPushButton::clicked,this,[this]{if(!running_){input_.clear();updateSource();}});
        connect(open,&QPushButton::clicked,this,[this]{
            if(running_)return;
            auto path=QFileDialog::getOpenFileName(this,QCoreApplication::translate("MonitorWindow", "Open SigMF recording"),
                QDir::homePath(),"SigMF (*.sigmf-meta)",nullptr,QFileDialog::DontUseNativeDialog);
            if(!path.isEmpty()){input_=path;updateSource();}
        });
        connect(refresh,&QPushButton::clicked,this,[this]{refreshFiles();});
        connect(analyze_file,&QPushButton::clicked,this,[this]{const auto row=files_->currentRow();if(row>=0)analyzeFile(files_->item(row,0)->data(Qt::UserRole).toString());else if(!input_.isEmpty())analyzeFile(input_);else{workspace_->show(files_);state_->setText(QCoreApplication::translate("MonitorWindow", "Select an I/Q recording first."));}});
        connect(export_file_,&QPushButton::clicked,this,[this]{
            if(closePending_ || exportJob_.busy())return;
            const auto row=files_->currentRow();if(row<0)return;
            const auto meta=files_->item(row,0)->data(Qt::UserRole).toString();
            const auto parent=QFileDialog::getExistingDirectory(this,QCoreApplication::translate("MonitorWindow", "Select an export location for permanent storage"),
                QDir::homePath(),QFileDialog::ShowDirsOnly|QFileDialog::DontUseNativeDialog);
            if(parent.isEmpty())return;
            const auto folder=QDir(parent).filePath(QFileInfo(meta).dir().dirName());
            export_file_->setEnabled(false);
            export_file_->setText(QCoreApplication::translate("MonitorWindow", "Exporting…"));
            try {
                exportJob_.start(QFile(meta).filesystemFileName(),QFile(folder).filesystemFileName(),
                                 [this,folder](auto failure){finishExport(folder,failure);});
            } catch(...) {
                finishExport(folder,std::current_exception());
            }
        });
        connect(files_,&QTableWidget::cellDoubleClicked,this,[this](int row,int){if(!running_){input_=files_->item(row,0)->data(Qt::UserRole).toString();updateSource();start();}});
        connect(events_,&QTableWidget::cellClicked,this,[this](int row,int){
            if(row<0 || row>=int(display_events_.size()))return;
            evidence_->setPlainText(desktop::eventEvidence(display_events_[static_cast<std::size_t>(row)]));
            workspace_->show(evidence_);
        });
        std::function<void()> stopPlayback;
        if(!live_tools_->panels().empty())stopPlayback=[this]{live_tools_->stopAll();};
        events_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(events_,&QWidget::customContextMenuRequested,this,[this](const QPoint& position){
            showEventActions(events_->rowAt(position.y()),position);
        });
        auto* eventMenuShortcut=new QShortcut(QKeySequence(Qt::SHIFT|Qt::Key_F10),events_);
        eventMenuShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        eventMenuShortcut->setKeys({QKeySequence(Qt::SHIFT|Qt::Key_F10),QKeySequence(Qt::Key_Menu)});
        connect(eventMenuShortcut,&QShortcut::activated,this,[this]{showSelectedEventActions();});
        toolboxes_=new desktop::ToolboxCenter(std::move(updates),[this]{return !running_ && !live_tools_->busy() && !analysis_->isAnalyzing() && !exportJob_.busy() && capabilityQuery_.state()==QProcess::NotRunning;},
            [this]{if(auto* action=findChild<QAction*>("manage-plugins"))action->trigger();},this);
        toolboxes_->updatesAvailable=[this](int count){if(count)statusBar()->showMessage(desktop::toolboxText("updates_available",{{"count",count}}));};
        menus_.mount(*this,{[this](QWidget* w){return workspace_->title(w);},[this](QWidget* w,bool v){workspace_->setVisible(w,v);},[this](QWidget* w,auto callback){workspace_->observe(w,std::move(callback));}},displays_,analysis_->plugins(),[this](const QString& id){
            if(analysis_->selectPlot(id))workspace_->show(analysis_);
        },stopPlayback,[this](const QString& id,bool enabled){analysis_->setPluginEnabled(id,enabled);},
        [this]{openToolboxes();},
        [this,pluginDirectory](const QString& id,const QString& name){return uninstallResearch(pluginDirectory,id,name);},[this]{openToolboxes();});
        auto* help=menuBar()->addMenu(QCoreApplication::translate("DesktopMenus","Help"));
        help->addAction(desktop::toolboxText("software"),this,[this]{toolboxes_->showSoftwareUpdates();});
        auto* licenses=help->addAction(QCoreApplication::translate("LegalNotices","Licenses and source"),this,[this]{desktop::showLegalNotices(this);});
        licenses->setObjectName("help/licenses-and-source");
        workspace_->restore([this](bool compact){defaultWorkspace(compact);});
        connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{workspace_->save();});
        timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,[this]{poll();});timer_.start();refreshFiles();
        connect(&capabilityQuery_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus state){
            capabilityDeadline_.stop();
            if(code==0 && state==QProcess::NormalExit)try {
                selection_.capabilities=Json::parse(capabilityQuery_.readAllStandardOutput().constData()).at("capabilities").get<ReceiverCapabilities>();
                selection_.manualGains=selection_.capabilities->manual_gains_tenths;
                loadGains(gain_->currentData().toString());
            } catch(...) {selection_.capabilities.reset();}
            if(!running_)start_->setEnabled(true);
            if(!running_)receiver_settings_->setEnabled(true);
            updateConfigurationFeedback();
        });
        connect(&capabilityQuery_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
            if(error!=QProcess::FailedToStart)return;
            capabilityDeadline_.stop();start_->setEnabled(true);receiver_settings_->setEnabled(true);updateConfigurationFeedback();
        });
        capabilityDeadline_.setSingleShot(true);
        connect(&capabilityDeadline_,&QTimer::timeout,this,[this]{capabilityQuery_.kill();});
        QTimer::singleShot(250,this,[this]{queryCapabilities();});
    }
    ~Window() override {delete toolboxes_;toolboxes_=nullptr;cancel_=true;live_tools_->stopAll();if(worker_.joinable())worker_.join();for(auto& [id,session]:receiverSessions_)session->disconnect();}
    void openToolboxes(){toolboxes_->show();toolboxes_->raise();toolboxes_->activateWindow();}
    bool triggerLiveAction(const Json& event,const QString& name) {
        QMenu menu;live_tools_->addEventActions(menu,event);
        for(auto* action:menu.actions())if(action->objectName()==name && action->isEnabled()){action->trigger();return true;}
        return false;
    }
    void checkUpdates(bool quiet=false){toolboxes_->checkForUpdates(quiet);}
    bool setFrequency(double mhz){
        if(running_ || !std::isfinite(mhz) || mhz<frequency_->minimum() || mhz>frequency_->maximum())return false;
        frequency_->setValue(mhz);
        return true;
    }
    bool setSearchWidth(std::uint32_t hz) {
        if(running_ || hz%1000 || hz/1000<static_cast<unsigned>(search_width_->minimum()) ||
           hz/1000>static_cast<unsigned>(search_width_->maximum()))return false;
        search_width_->setValue(static_cast<int>(hz/1000));
        return true;
    }
    bool setGain(QString value){if(value!="auto"){bool ok=false;const auto gain=value.toDouble(&ok);if(!ok || !std::isfinite(gain) || gain<-100 || gain>100)return false;value=QString::number(gain,'f',1);}auto index=gain_->findData(value);if(index<0){gain_->addItem(value,value);index=gain_->count()-1;}gain_->setCurrentIndex(index);return true;}
    bool setDsp(const QString& rate,const QString& workers,const QString& threshold){
        bool rate_ok=true,workers_ok=true,threshold_ok=true;
        const auto r=rate.isEmpty()?sample_rate_hz_:rate.toUInt(&rate_ok);
        const auto w=workers.isEmpty()?detection_workers_:workers.toInt(&workers_ok);
        const auto t=threshold.isEmpty()?threshold_db_:threshold.toDouble(&threshold_ok);
        if(!rate_ok||!workers_ok||!threshold_ok||r<1||r>100000000||
           w<1||w>32||!std::isfinite(t)||t<6||t>60)return false;
        sample_rate_hz_=r;detection_workers_=w;threshold_db_=t;updateCoverage();return true;
    }
    void enableDiagnostics(){diagnostics_=true;}
    void analyzeFile(const QString& metadata){workspace_->show(analysis_);analysis_->setThreshold(threshold_db_);analysis_->analyze(metadata);}
    bool hasAnalysisResult() const {return analysis_->hasResult();}
    Json sessionDiagnostics() const {return rendered_status_;}
    bool restorePreviousAnalysis() {return analysis_->restorePreviousArtifact();}
    bool exportAnalysisResult(const QString& directory) {return analysis_->exportResultToDirectory(directory);}
    bool smokeReplayNarrowband() const {
        const auto has=[this](const char* kind){
            return std::any_of(display_events_.begin(),display_events_.end(),[kind](const auto& event){
                return event.value("evidence_level",std::string())==kind;
            });
        };
        return has("fsk_candidate") && has("ook_candidate") && has("ask_candidate");
    }
    bool smokeResearchEvent(bool fm) {
        const auto event=std::find_if(display_events_.begin(),display_events_.end(),[fm](const auto& value){
            return value.value("evidence_level",std::string())==(fm?"broadcast_fm_stereo":"lora_structure_match");
        });
        if(event==display_events_.end())return false;
        const int row=static_cast<int>(std::distance(display_events_.begin(),event));
        workspace_->show(events_);
        events_->setCurrentCell(row,0);
        QTimer::singleShot(0,this,[fm]{
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu)return;
            const auto actions=menu->actions();
            const auto lora=std::find_if(actions.begin(),actions.end(),[fm](const auto* action){
                return action->objectName()==(fm?"research-event-fm":"research-event-lora");
            });
            if(lora==actions.end()){menu->close();return;}
            menu->setActiveAction(*lora);
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(menu,&key);
            if(menu->isVisible())menu->close();
        });
        showSelectedEventActions();
        return analysis_->isAnalyzing() && workspace_->visible(analysis_);
    }
    QString eventModulationHeading() const {
        const auto* heading=events_->horizontalHeaderItem(0);
        return heading?heading->text():QString();
    }

    bool recordingTooltipContains(const QString& original,const QString& heading) const {
        if(original.isEmpty() || heading.isEmpty())return false;
        for(int row=0;row<files_->rowCount();++row) {
            const auto* item=files_->item(row,0);
            if(item && item->toolTip().contains(original.toHtmlEscaped()) &&
               item->toolTip().contains(heading.toHtmlEscaped()))return true;
        }
        return false;
    }

    bool sessionControlsFit() const {
        for(const auto* button:{start_,stop_,pause_,receiver_settings_}) {
            const auto topLeft=button->mapTo(const_cast<Window*>(this),QPoint());
            if(!button->isVisible() || !rect().contains(QRect(topLeft,button->size())) || !button->visibleRegion().contains(button->rect()))
                return false;
        }
        return true;
    }
    Json liveToolDiagnostics() const {return live_tools_->diagnostics();}
    QString runtimeText(const std::string& key) const {return runtimeStatus_->text(key);}
    bool eventLayoutFits(bool compact) const {
        if(displays_.empty())return false;
        const bool grouped=tabifiedDockWidgets(workspace_->dock(displays_.front())).contains(workspace_->dock(events_));
        const bool plot=std::any_of(displays_.begin(),displays_.end(),[](const auto* d){return d->visibleRegion().boundingRect().height()>=60;});
        return compact==grouped && plot;
    }
    void compactWorkspace(){setWindowState(Qt::WindowNoState);workspace_->reset(true);}
    void monitorWorkspace(){setWindowState(Qt::WindowNoState);workspace_->reset(false);}
    void wideWorkspace(){setWindowState(Qt::WindowNoState);resize(1920,1080);workspace_->reset(false);}
    bool selectWorkspaceLayout(const QString& id) {
        auto* action=findChild<QAction*>("workspace-layout/"+id);if(!action)return false;action->trigger();return true;
    }
    bool researchWorkspaceFits(const QString& id,bool requireRestored=false) const {
        if(requireRestored && !workspace_->restoredFromSettings())return false;
        const auto& layouts=live_tools_->layouts();const auto selected=std::find_if(layouts.begin(),layouts.end(),[&](const auto& layout){return layout.id==id;});
        if(selected==layouts.end() || !sessionControlsFit() || selected->primary->visibleRegion().boundingRect().width()<150 || !workspace_->visible(events_))return false;
        if(width()>=1200 && height()>=720)for(const auto& panel:selected->panels)if(panel.widget->visibleRegion().boundingRect().width()<150)return false;
        return true;
    }
    bool wideWorkspaceFits() const {
        int visible=0;
        for(auto* pane:findChildren<QDockWidget*>()) {
            if(pane->visibleRegion().boundingRect().width()>150 && pane->visibleRegion().boundingRect().height()>80)++visible;
        }
        return width()>=1600 && visible>=6 && sessionControlsFit() &&
            displays_.front()->visibleRegion().boundingRect().height()>80;
    }
    bool selectAnalysisPlot(const QString& id) {return analysis_->selectPlot(id);}
    bool isSessionRunning() const {return running_;}
    void stopListening(){if(running_)stop_->click();}
    bool replayCompleted() {
        std::lock_guard lock(shared_.mutex);
        const auto& status=shared_.status;
        const auto expected=status.value("source_pairs",std::uint64_t{});
        return status.value("state",std::string{})=="replay_finished" &&
            expected>0 && status.value("processed_pairs",std::uint64_t{})==expected;
    }
    void start() {
        if(running_)return;if(worker_.joinable())worker_.join();cancel_=false;paused_=false;
        state_->setToolTip({});
        std::uint64_t replayPairs=0;
        std::uint32_t replayRate=0;
        if(!input_.isEmpty()) {
            try {
                const auto info=inspect_recording(QFile(input_).filesystemFileName());
                frequency_->setValue(info.frequency/1e6);
                replayPairs=info.pairs;
                replayRate=info.rate;
            }
            catch(const std::exception& e){
                desktop::show_error(*state_,e,QCoreApplication::translate("MonitorWindow", "Could not open the recording."));
                return;
            }
        }
        Config cfg=receiverConfiguration();
        const auto gain=gain_->currentData().toString();
        cfg.auto_gain=gain=="auto";if(!cfg.auto_gain)cfg.gain_tenths=static_cast<int>(std::lround(gain.toDouble()*10));
        LoRaConfig lc;lc.rate=cfg.rate;lc.receiver_frequency=cfg.frequency;
        lc.search_width=search_width_->value()*1000.0;
        lc.workers=static_cast<unsigned>(detection_workers_);lc.peak_to_mean_db=threshold_db_;
        lc.bandwidth_candidates=defaultLiveLoRaBandwidths();
        const auto detectors=menus_.selection();
        const auto inputRate=replayRate?replayRate:cfg.rate;
        ReceiverCapabilities caps;
        caps.backend="recorded";caps.format=receiver_->format;caps.sample_rates={inputRate};
        if(input_.isEmpty() && selection_.capabilities)caps=*selection_.capabilities;
        auto planned=cfg;planned.rate=inputRate;
        // Without a device query, the session still performs authoritative hardware validation.
        if(!input_.isEmpty() || !selection_.capabilities) {planned.antenna.clear();caps.manual_gains_known=false;}
        const auto plan=planMonitorConfiguration(caps,planned,lc,detectors.lora,detectors.narrowband.any(),detectors.snapshot.any());
        if(!plan.valid()) {
            state_->setText(desktop::receiverPlanProblemSummary(plan));
            state_->setToolTip(desktop::receiverPlanSummary(caps,planned,plan));
            state_->setStyleSheet("color:#ffb06c");return;
        }
        RetentionPolicy retention;retention.quota_bytes=static_cast<std::uint64_t>(quota_->value())*1024*1024;
        retention.max_event_seconds=limit_->value();retention.max_event_bytes=static_cast<std::uint64_t>(std::ceil(cfg.rate*bytes_per_pair(receiver_->format)*retention.max_event_seconds));
        if(retention.quota_bytes<retention.max_event_bytes+1024*1024){state_->setText(QCoreApplication::translate("MonitorWindow", "The quota cannot hold a maximum-length clip. Increase the quota or shorten the clip limit."));return;}
        retention.diagnostic_candidates=diagnostics_;
        const auto input=QFile(input_).filesystemFileName(),output=QFile(output_).filesystemFileName();
        if(input.empty()){QSettings settings;settings.setValue(settingKey("frequency_mhz"),frequency_->value());settings.setValue(settingKey("gain"),gain);settings.setValue("receiver/max_event_seconds",limit_->value());settings.setValue(settingKey("sample_rate_hz"),sample_rate_hz_);settings.setValue(settingKey("search_width_khz"),search_width_->value());settings.setValue(settingKey("search_width_mode"),search_width_->mode());settings.setValue("receiver/detection_workers",detection_workers_);settings.setValue("receiver/threshold_db",threshold_db_);}
        {std::lock_guard lock(shared_.mutex);shared_.error=nullptr;shared_.done=false;shared_.status=Json::object();shared_.spectrum={};}
        extended_.reset(detectors.snapshot);
        replayEventRecording_=input_;
        replayEventPairs_=replayPairs;
        replayEventRate_=replayRate;
        menus_.setSessionRunning(true);
        running_=true;receiver_settings_->setEnabled(false);start_->setEnabled(false);stop_->setEnabled(true);pause_->setEnabled(!input_.isEmpty());frequency_->setEnabled(false);search_width_->setEnabled(false);gain_->setEnabled(false);quota_->setEnabled(false);limit_->setEnabled(false);
        live_tools_->setSession(input_.isEmpty());
        for(auto* display:displays_)display->reset();events_->setRowCount(0);display_events_=Json::array();
        rendered_status_=Json::object();rendered_spectrum_sequence_=0;
        power_->setText(QCoreApplication::translate("MonitorWindow", "Waiting for new I/Q…"));
        storage_->setText(input_.isEmpty()?QCoreApplication::translate("MonitorWindow", "Pre-trigger I/Q stays in memory. Confirmed events are saved to disk."):QCoreApplication::translate("MonitorWindow", "Replay analyzes the original recording without copying I/Q."));
        evidence_->setPlainText(QCoreApplication::translate("MonitorWindow", "Select a detected event to inspect its measurements and raw evidence."));
        state_->setText(input_.isEmpty()?QCoreApplication::translate("MonitorWindow", "Initializing the receiver…"):QCoreApplication::translate("MonitorWindow", "Opening recording…"));
        state_->setToolTip({});
        const auto session=input.empty()?receiverSession(cfg):std::shared_ptr<ReceiverSession>{};
        worker_=std::jthread([this,cfg,lc,retention,input,output,detectors,receiver=receiver_,session]() mutable {
            MonitorObserver observer;
            observer.enable_lora=detectors.lora;
            observer.narrowband=detectors.narrowband;
            if(input.empty())observer.capture_iq.add(live_tools_->subscriber());
            if(input.empty() && detectors.snapshot.any())
                observer.analysis_iq.add(extended_.makeFeed(lc.search_width));
            observer.status=[this](const Json& status){std::lock_guard lock(shared_.mutex);shared_.status=status;};
            observer.spectrum=[this](const SpectrumFrame& frame){std::lock_guard lock(shared_.mutex);shared_.spectrum=frame;};
            try {
                if(input.empty()) {
                    auto sharedReceiver=*receiver;
                    sharedReceiver.probe=[session](auto,auto){return session->describe();};
                    sharedReceiver.stream=[session](const auto& c,const auto& sink,const auto& stop,const auto& ready){return session->stream(c,sink,stop,ready);};
                    monitor(cfg,lc,retention,output,input,[this]{return cancel_.load();},observer,sharedReceiver);
                }
                else {
                    const auto info=inspect_recording(input);
                    ReplaySessionObserver replayObserver{observer.status,observer.spectrum,{}};
                    replayObserver.analysis_iq.add(extended_.makeFeed(lc.search_width));
                    replaySession(info,{lc,detectors.lora,detectors.narrowband},
                        [this]{return paused_.load();},[this]{return cancel_.load();},replayObserver);
                }
            } catch(const std::exception&) {std::lock_guard lock(shared_.mutex);shared_.error=std::current_exception();}
            std::lock_guard lock(shared_.mutex);shared_.done=true;
        });
    }
protected:
    void closeEvent(QCloseEvent* event) override {
        if(running_ || exportJob_.busy()) {
            closePending_=true;
            if(running_)requestStop();
            event->ignore();
            return;
        }
        event->accept();
    }
    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        if(source_)elideSource();
    }
private:
    void researchWorkspace(const desktop::LiveResearchWorkspace& layout) {
        const bool compact=width()<1200 || height()<720;
        std::vector<desktop::WorkspaceLayoutItem> items{{controlsPane_,Qt::LeftDockWidgetArea},{statusPane_,Qt::LeftDockWidgetArea,controlsPane_,Qt::Vertical},
            {layout.primary,Qt::RightDockWidgetArea}};
        for(const auto& panel:layout.panels)items.push_back({panel.widget,panel.preferredArea,
            compact?layout.primary:panel.relativeTo?panel.relativeTo:layout.primary,panel.orientation,compact});
        items.push_back({events_,compact?Qt::RightDockWidgetArea:Qt::BottomDockWidgetArea,compact?layout.primary:nullptr,Qt::Vertical,compact});
        workspace_->applyLayout(items);
        const auto sidebar=compact?240:280;
        if(!compact && !layout.panels.empty())resizeDocks({workspace_->dock(controlsPane_),workspace_->dock(layout.primary),workspace_->dock(layout.panels.front().widget)},
            {sidebar,std::max(360,width()-sidebar-360),360},Qt::Horizontal);
        else resizeDocks({workspace_->dock(controlsPane_),workspace_->dock(layout.primary)},{sidebar,std::max(260,width()-sidebar)},Qt::Horizontal);
        if(!compact) {
            resizeDocks({workspace_->dock(controlsPane_),workspace_->dock(statusPane_)},{std::max(390,height()*3/5),std::max(180,height()*2/5)},Qt::Vertical);
            resizeDocks({workspace_->dock(layout.primary),workspace_->dock(events_)},{std::max(300,height()*5/6),std::max(100,height()/6)},Qt::Vertical);
        }
        workspace_->show(controlsPane_);workspace_->show(layout.primary);
    }
    void defaultWorkspace(bool compact) {
        auto* controls=workspace_->dock(controlsPane_);auto* status=workspace_->dock(statusPane_);
        addDockWidget(Qt::LeftDockWidgetArea,controls);addDockWidget(Qt::LeftDockWidgetArea,status);
        splitDockWidget(controls,status,Qt::Vertical);
        std::vector<QDockWidget*> visual;
        for(auto* display:displays_){auto* pane=workspace_->dock(display);addDockWidget(Qt::RightDockWidgetArea,pane);visual.push_back(pane);}
        const auto first=visual.front();
        for(std::size_t i=1;i<visual.size();++i) {
            if(!compact && i==1)splitDockWidget(first,visual[i],Qt::Vertical);
            else if(compact)tabifyDockWidget(first,visual[i]);
        }
        auto* evidence=workspace_->dock(evidence_);auto* recordings=workspace_->dock(files_);auto* analysis=workspace_->dock(analysis_);
        for(auto* pane:{evidence,recordings,analysis})addDockWidget(Qt::RightDockWidgetArea,pane);
        auto* events=workspace_->dock(events_);addDockWidget(Qt::BottomDockWidgetArea,events);
        if(compact) {
            for(auto* pane:{events,evidence,recordings,analysis})tabifyDockWidget(first,pane);
        } else {
            splitDockWidget(first,evidence,Qt::Horizontal);
            tabifyDockWidget(evidence,recordings);tabifyDockWidget(evidence,analysis);
            for(std::size_t i=2;i<visual.size();++i)tabifyDockWidget(first,visual[i]);
            resizeDocks({controls,first,evidence},{280,std::max(440,width()-800),440},Qt::Horizontal);
            resizeDocks({controls,status},{360,240},Qt::Vertical);
            resizeDocks({first,visual.size()>1?visual[1]:first,events},{380,260,220},Qt::Vertical);
            evidence->raise();
        }
        for(const auto& panel:live_tools_->panels()) {
            if(panel.relativeTo)workspace_->place(panel.widget,panel.relativeTo,panel.orientation,compact);
            else tabifyDockWidget(compact?first:analysis,workspace_->dock(panel.widget));
        }
        controls->raise();first->raise();
        QTimer::singleShot(0,this,[this,controls,status,first,evidence,events,compact,visual]{
            const auto sidebar=std::clamp(width()/7,280,340);
            if(compact)resizeDocks({controls,first},{sidebar,std::max(280,width()-sidebar)},Qt::Horizontal);
            else {
                const auto detail=std::clamp(width()/4,380,600);
                resizeDocks({controls,first,evidence},{sidebar,std::max(400,width()-sidebar-detail),detail},Qt::Horizontal);
                resizeDocks({controls,status},{std::max(360,height()*2/3),std::max(200,height()/3)},Qt::Vertical);
                resizeDocks({first,visual.size()>1?visual[1]:first,events},{height()*4/10,height()*3/10,height()*3/10},Qt::Vertical);
            }
            controls->raise();first->raise();if(!compact)evidence->raise();
        });
    }
    bool uninstallResearch(const QString& directory,const QString& id,const QString& name) {
        const auto title=QCoreApplication::translate("MonitorWindow", "Uninstall research plugin");
        if(analysis_->isAnalyzing()) {
            QMessageBox::information(this,title,QCoreApplication::translate("MonitorWindow", "Finish or cancel the current analysis before uninstalling a plugin."));
            return false;
        }
        QMessageBox question(QMessageBox::Question,title,
            QCoreApplication::translate("MonitorWindow", "Uninstall %1? Its package will be kept outside the plugin loading folder.").arg(name),
            QMessageBox::Yes|QMessageBox::Cancel,this);
        question.setTextFormat(Qt::PlainText);
        question.setDefaultButton(QMessageBox::Cancel);
        if(question.exec()!=QMessageBox::Yes)return false;
        const auto archive=desktop::removeResearchPlugin(id,directory);
        if(archive.isEmpty()) {
            QMessageBox::warning(this,title,QCoreApplication::translate("MonitorWindow", "Could not move the plugin out of the loading folder."));
            return false;
        }
        analysis_->forgetPlugin(id);
        QMessageBox done(QMessageBox::Information,title,
            QCoreApplication::translate("MonitorWindow", "Plugin uninstalled. Package kept at %1").arg(QDir::toNativeSeparators(archive)),
            QMessageBox::Ok,this);
        done.setTextFormat(Qt::PlainText);
        done.exec();
        return true;
    }
    void installResearch(const QString& directory) {
        const auto title=QCoreApplication::translate("MonitorWindow", "Install research plugin");
        const auto manifest=QFileDialog::getOpenFileName(this,title,QDir::homePath(),
            QCoreApplication::translate("MonitorWindow", "Plugin manifest (*.json)"),nullptr,QFileDialog::DontUseNativeDialog);
        if(manifest.isEmpty())return;
        const auto confirm=QMessageBox::question(this,title,QCoreApplication::translate("MonitorWindow",
            "Research plugins run with your user permissions. Install this plugin only if you trust its source."),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
        if(confirm!=QMessageBox::Yes)return;
        using Result=desktop::ResearchInstallResult;
        switch(desktop::installResearchPlugin(manifest,directory)) {
        case Result::installed:
            QMessageBox::information(this,title,QCoreApplication::translate("MonitorWindow", "Plugin installed. Restart Foreshadow to load it."));
            break;
        case Result::invalid_package:
            QMessageBox::warning(this,title,QCoreApplication::translate("MonitorWindow", "The plugin manifest or executable is invalid."));
            break;
        case Result::duplicate_id:
            QMessageBox::warning(this,title,QCoreApplication::translate("MonitorWindow", "A plugin with this ID is already installed."));
            break;
        case Result::storage_error:
            QMessageBox::warning(this,title,QCoreApplication::translate("MonitorWindow", "Could not save the plugin. Check disk space and permissions."));
            break;
        }
    }
    void finishExport(const QString& folder,std::exception_ptr failure) {
        export_file_->setEnabled(true);
        export_file_->setText(QCoreApplication::translate("MonitorWindow", "Export"));
        if(failure) {
            const auto title=QCoreApplication::translate("MonitorWindow", "Export failed");
            QMessageBox warning(QMessageBox::Warning,title,title,QMessageBox::Ok,this);
            try {std::rethrow_exception(failure);}
            catch(const std::exception& error) {
                warning.setText(desktop::error_summary(error,title));
                warning.setDetailedText(QString::fromUtf8(error.what()));
            } catch(...) {}
            warning.exec();
        } else if(!closePending_) {
            const auto text=QCoreApplication::translate("MonitorWindow", "Recording exported to %1").arg(folder);
            QToolTip::showText(export_file_->mapToGlobal(export_file_->rect().bottomLeft()),"<p>"+text.toHtmlEscaped()+"</p>",export_file_);
        }
        if(closePending_ && !running_)close();
    }
    void requestStop() {
        if(cancel_.exchange(true))return;
        stop_->setEnabled(false);
        live_tools_->setSession(false);
        state_->setText(desktop::sessionStatus("analysis_finished"));
    }
    void showSelectedEventActions() {
        const auto* item=events_->currentItem();
        if(!item)return;
        events_->scrollToItem(item);
        showEventActions(item->row(),events_->visualItemRect(item).center());
    }
    void showEventActions(int row,const QPoint& position) {
        if(row<0 || row>=int(display_events_.size()))return;
        const auto& event=display_events_[static_cast<std::size_t>(row)];
        const auto level=event.value("evidence_level",std::string());
        const auto center=event.value("center_frequency_hz",Json(nullptr));
        if(!replayEventRecording_.isEmpty() && replayEventRecording_==input_) {
            const auto range=desktop::replayEventRange(event,replayEventRate_,replayEventPairs_);
            const auto metadata=replayEventRecording_;
            QMenu actions(this);
            live_tools_->addRecordingActions(actions,{QFile(metadata).filesystemFileName(),range,event});
            const auto providerActions=actions.actions();
            if(!range){if(!actions.isEmpty())actions.exec(events_->viewport()->mapToGlobal(position));return;}
            auto* overview=actions.addAction(QCoreApplication::translate("MonitorWindow", "Analyze event in I/Q overview"));
            auto* lora=level=="lora_structure_match"
                ?actions.addAction(QCoreApplication::translate("MonitorWindow", "Analyze event in LoRa structure")):nullptr;
            auto* fm=level=="broadcast_fm_stereo" && center.is_number()
                ?actions.addAction(QCoreApplication::translate("MonitorWindow", "Demodulate recorded FM event")):nullptr;
            overview->setEnabled(!analysis_->isAnalyzing());
            if(lora) {
                lora->setObjectName("research-event-lora");
                lora->setEnabled(!analysis_->isAnalyzing());
            }
            if(fm) {
                fm->setObjectName("research-event-fm");
                fm->setEnabled(!analysis_->isAnalyzing());
            }
            const auto* selected=actions.exec(events_->viewport()->mapToGlobal(position));
            if(!selected || input_!=metadata || providerActions.contains(const_cast<QAction*>(selected)))return;
            analysis_->setThreshold(threshold_db_);
            auto selectedRange=*range;
            std::optional<double> frequency;
            if(selected==fm) {
                frequency=center.get<double>();
                selectedRange.count=std::max(selectedRange.count,std::min<std::uint64_t>(
                    replayEventPairs_-selectedRange.first,5ULL*replayEventRate_));
            }
            if(analysis_->analyzeRange(metadata,selected==fm?"fm-audio":selected==lora?"lora-structure":"iq-overview",
                                      selectedRange,frequency))
                workspace_->show(analysis_);
            return;
        }
        QMenu actions(this);
        live_tools_->addEventActions(actions,event);
        if(!actions.isEmpty())actions.exec(events_->viewport()->mapToGlobal(position));
    }

    void updateCoverage(){
        if(!coverage_)return;
        coverage_->setWordWrap(false);
        double rate=sample_rate_hz_,frequency=frequency_->value();
        if(!input_.isEmpty())try{const auto info=inspect_recording(QFile(input_).filesystemFileName());rate=info.rate;frequency=info.frequency/1e6;}catch(const std::exception&){}
        const auto half=search_width_->value()/2000.0;
        coverage_->setText(QCoreApplication::translate("MonitorWindow", "%1 MS/s · Search %2–%3 MHz")
            .arg(desktop::displayDecimal(rate/1e6,3),desktop::displayDecimal(frequency-half,3),desktop::displayDecimal(frequency+half,3)));
    }
    QString settingKey(const char* key) const {
        return desktop::receiverSettingsPrefix(QString::fromLatin1(receiver_->id))+QString::fromLatin1(key);
    }
    void loadReceiverPreferences() {
        QSettings settings;
        selection_.inputMode=settings.value(settingKey("input_mode"),"auto").toString();
        selection_.ifBandwidth=settings.value(settingKey("if_bandwidth_hz"),0.).toDouble();
        selection_.settings=Json::parse(settings.value(settingKey("settings"),"{}").toString().toStdString(),nullptr,false);
        if(!selection_.settings.is_object())selection_.settings=Json::object();
        frequency_->setRange(.000001,9007199254.740991);
        search_width_->setSelection(settings.value(settingKey("search_width_khz"),1000).toInt(),settings.value(settingKey("search_width_mode"),"fixed").toString());
        sample_rate_hz_=settings.value(settingKey("sample_rate_hz"),2000000).toUInt();
        if(!sample_rate_hz_ || sample_rate_hz_>100000000)sample_rate_hz_=2000000;
        frequency_->setValue(settings.value(settingKey("frequency_mhz"),470.5).toDouble());
        loadGains(settings.value(settingKey("gain"),QString::fromLatin1(receiver_->default_gain)).toString());
    }
    void loadGains(const QString& preferred) {
        gain_label_->setText(receiver_->gain_kind==ReceiverGainKind::if_reduction
            ?QCoreApplication::translate("MonitorWindow", "IF gain reduction (dB)")
            :QCoreApplication::translate("MonitorWindow", "Gain dB"));
        gain_->clear();
        const auto presets=selection_.manualGains?std::span<const int>(*selection_.manualGains):receiver_->gain_presets_tenths;
        for(const auto gain:presets) {
            const auto value=QString::number(gain/10.0,'f',1);
            gain_->addItem(desktop::displayDecimal(gain/10.0,1),value);
        }
        gain_->addItem(QCoreApplication::translate("MonitorWindow", "Automatic gain"),QStringLiteral("auto"));
        const auto defaultGain=QString::fromLatin1(receiver_->default_gain);
        if(!setGain(preferred) && !setGain(defaultGain))setGain(QStringLiteral("auto"));
    }
    void chooseReceiver() {
        if(running_){workspace_->show(controlsPane_);state_->setText(runtimeStatus_->text("settings_monitor_busy"));return;}
        auto current=selection_;
        current.sampleRate=sample_rate_hz_;
        current.frequency=static_cast<std::uint64_t>(std::llround(frequency_->value()*1e6));
        current.searchWidth=search_width_->value()*1000.;current.gain=gain_->currentData().toString();
        current.searchWidthMode=search_width_->mode();
        const auto detectors=menus_.selection();current.enableLora=detectors.lora;
        current.narrowband=detectors.narrowband.any();current.snapshots=detectors.snapshot.any();
        current.narrowbandDetectors=detectors.narrowband;current.snapshotDetectors=detectors.snapshot;
        current.lora=LoRaConfig{};current.lora.bandwidth_candidates=defaultLiveLoRaBandwidths();
        desktop::ReceiverDialog dialog(current,this,[this](const desktop::ReceiverSelection& selected){
            const auto session=findReceiverSession(selected.backend.toStdString(),selected.index,selected.serial.toStdString());if(!session)return QString();
            const auto state=session->snapshot().value("state",std::string());
            return state=="receiving" || state=="starting" || state=="stopping"?runtimeStatus_->text("settings_busy"):QString();
        });
        if(dialog.exec()!=QDialog::Accepted)return;
        const auto selected=dialog.selection();
        const bool changed_backend=selected.backend!=selection_.backend;
        const auto previousGain=gain_->currentData().toString();
        selection_=selected;
        std::erase_if(receiverSessions_,[](const auto& item){const auto state=item.second->snapshot().value("state",std::string());return state=="idle" || state=="disconnected";});
        receiver_=&receiver_by_name(selection_.backend.toStdString());
        QSettings settings;
        settings.setValue("desktop/receiver_backend",selection_.backend);
        settings.setValue(settingKey("device"),selection_.index);
        settings.setValue(settingKey("serial"),selection_.serial);
        settings.setValue(settingKey("antenna"),selection_.antenna);
        settings.setValue(settingKey("input_mode"),selection_.inputMode);
        settings.setValue(settingKey("if_bandwidth_hz"),selection_.ifBandwidth);
        settings.setValue(settingKey("settings"),QString::fromStdString(selection_.settings.dump()));
        settings.setValue(settingKey("sample_rate_hz"),selected.sampleRate);
        settings.setValue(settingKey("frequency_mhz"),selected.frequency/1e6);
        settings.setValue(settingKey("search_width_khz"),selected.searchWidth/1000);
        settings.setValue(settingKey("search_width_mode"),selected.searchWidthMode);
        settings.setValue(settingKey("gain"),selected.gain);
        if(changed_backend)loadReceiverPreferences();
        else loadGains(previousGain);
        sample_rate_hz_=selected.sampleRate;
        frequency_->setValue(selected.frequency/1e6);search_width_->setSelection(static_cast<int>(selected.searchWidth/1000),selected.searchWidthMode);
        loadGains(selected.gain);
        input_.clear();
        updateSource();
        state_->setText(desktop::receiverAppliedSummary(selected.applied));
        updateConfigurationFeedback();
    }
    void queryCapabilities() {
        if(running_ || receptionBusy() || !input_.isEmpty() || QApplication::activeModalWidget() || capabilityQuery_.state()!=QProcess::NotRunning)return;
        QStringList args{"probe","--receiver",QString::fromLatin1(receiver_->id),"--device",QString::number(selection_.index)};
        if(!selection_.serial.isEmpty())args.append({"--serial",selection_.serial});
        start_->setEnabled(false);receiver_settings_->setEnabled(false);capabilityDeadline_.start(5000);
        capabilityQuery_.start(desktop::companion_cli(),args);
    }
    void updateConfigurationFeedback() {
        if(!configuration_feedback_ || running_)return;
        if(!input_.isEmpty()){configuration_feedback_->clear();return;}
        if(!selection_.capabilities) {
            configuration_feedback_->setText(QCoreApplication::translate("ReceiverConfiguration","Device capability query unavailable. Open receiver settings to query the connected device."));return;
        }
        Config c;c.rate=sample_rate_hz_;c.frequency=static_cast<std::uint64_t>(std::llround(frequency_->value()*1e6));
        c.antenna=selection_.antenna.toStdString();c.auto_gain=gain_->currentData().toString()=="auto";
        c.input_mode=selection_.inputMode.toStdString();c.if_bandwidth_hz=selection_.ifBandwidth;c.receiver_settings=selection_.settings;
        for(const auto& d:receiverParameters(*selection_.capabilities,c))
            if(d.value("key",std::string())=="auto_gain")gain_->setEnabled(d.value("enabled",true));
        if(!c.auto_gain)c.gain_tenths=static_cast<int>(std::lround(gain_->currentData().toDouble()*10));
        LoRaConfig lc;lc.search_width=search_width_->value()*1000.;
        lc.bandwidth_candidates=defaultLiveLoRaBandwidths();
        const auto detectors=menus_.selection();
        auto p=planMonitorConfiguration(*selection_.capabilities,c,lc,detectors.lora,detectors.narrowband.any(),detectors.snapshot.any());
        {const QSignalBlocker blocked(search_width_);search_width_->setAvailableWidth(p.maximum_search_width_hz);}
        lc.search_width=search_width_->value()*1000.;
        p=planMonitorConfiguration(*selection_.capabilities,c,lc,detectors.lora,detectors.narrowband.any(),detectors.snapshot.any());
        updateCoverage();
        const auto detail=desktop::receiverPlanSummary(*selection_.capabilities,c,p);
        configuration_feedback_->setText(p.valid()?
            QCoreApplication::translate("ReceiverConfiguration","Receiver configuration compatible · Maximum search %1 kHz · %2 MB/s")
                .arg(p.maximum_search_width_hz/1000,0,'f',0).arg(p.bytes_per_second/1e6,0,'f',2):
            desktop::receiverPlanProblemSummary(p));
        configuration_feedback_->setStyleSheet(p.valid()?"":"color:#ffb06c");configuration_feedback_->setToolTip(detail);
    }
    void elideSource() {
        source_->setText(source_->fontMetrics().elidedText(sourceText_,Qt::ElideMiddle,source_->contentsRect().width()));
    }
    void updateSource(){
        sourceText_=input_.isEmpty()?QCoreApplication::translate("MonitorWindow", "Source: live %1 · Event folder: %2").arg(QString::fromLatin1(receiver_->display_name),output_):QCoreApplication::translate("MonitorWindow", "Source: recording replay (receive-only, no I/Q copy) · ")+input_;
        source_->setToolTip("<p>"+sourceText_.toHtmlEscaped()+"</p>");
        elideSource();
        start_->setText(input_.isEmpty()?QCoreApplication::translate("MonitorWindow", "Start listening"):QCoreApplication::translate("MonitorWindow", "Start replay"));
        updateCoverage();
    }
    void refreshFiles() {
        const auto region=locale();
        const auto zone=QTimeZone::systemTimeZone();
        files_->setRowCount(0);QDirIterator it(output_,{"capture.sigmf-meta"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()&&files_->rowCount()<256) {
            const auto path=it.next();try {
                const auto info=inspect_recording(QFile(path).filesystemFileName());const auto row=files_->rowCount();files_->insertRow(row);
                auto* item=new QTableWidgetItem(QFileInfo(path).dir().dirName());item->setData(Qt::UserRole,path);files_->setItem(row,0,item);
                const auto timestamp=desktop::recordingTimeTooltip(info.metadata,region,zone);
                if(!timestamp.isEmpty())item->setToolTip("<pre>"+timestamp.toHtmlEscaped()+"</pre>");
                files_->setItem(row,1,new QTableWidgetItem(desktop::displayDecimal(double(info.pairs)/info.rate,3)));
                const auto status=desktop::recordingStatus(info.metadata);
                auto* stateItem=new QTableWidgetItem(status.label);
                if(!status.code.isEmpty())stateItem->setToolTip("<pre>"+status.code.toHtmlEscaped()+"</pre>");
                files_->setItem(row,2,stateItem);
                files_->setItem(row,3,new QTableWidgetItem(desktop::displayDecimal(info.frequency/1e6,6)));
            } catch(...) {}
        }
        workspace_->setTitle(files_,QCoreApplication::translate("MonitorWindow", "Recordings and replay (%1)").arg(files_->rowCount()));
    }
    void poll() {
        updateRuntimeStatus();
        updateConfigurationFeedback();
        extended_.poll();
        SpectrumFrame frame;Json status;std::exception_ptr error;bool done=false;
        {std::lock_guard lock(shared_.mutex);frame=shared_.spectrum;status=shared_.status;error=shared_.error;done=shared_.done;}
        if(!status.empty()) {
            status["extended_events"]=extended_.events();
            status["extended_available"]=extended_.available();
            status["snapshot_analysis"]=extended_.status();
        }
        if(!frame.bins_dbfs.empty() && frame.sequence!=rendered_spectrum_sequence_){
            rendered_spectrum_sequence_=frame.sequence;
            for(auto* display:displays_)
                if(workspace_->visible(display))display->setFrame(frame);
            power_->setText(QCoreApplication::translate("MonitorWindow", "Full-band level %1 dBFS · Uncalibrated RF power · Sample %2")
                .arg(desktop::displayDecimal(frame.power_dbfs,1),QString::number(qulonglong(frame.first_sample))));
            const auto half=frame.search_width_hz/2;
            auto coverage=QCoreApplication::translate("MonitorWindow", "Capture %1–%2 MHz · Search %3–%4 MHz")
                .arg(desktop::displayDecimal((frame.frequency-frame.rate/2.0)/1e6,3),
                     desktop::displayDecimal((frame.frequency+frame.rate/2.0)/1e6,3),
                     desktop::displayDecimal((frame.frequency-half)/1e6,3),desktop::displayDecimal((frame.frequency+half)/1e6,3));
            if(frame.filter_bandwidth_hz>0)
                coverage+=QCoreApplication::translate("MonitorWindow", " · IF filter %1 MHz").arg(desktop::displayDecimal(frame.filter_bandwidth_hz/1e6,3));
            coverage_->setWordWrap(frame.filter_bandwidth_hz>0);
            coverage_->setText(coverage);
        }
        if(!status.empty() && status!=rendered_status_) {
            rendered_status_=status;
            const auto dropped=status.value("analysis_dropped_pairs",std::uint64_t(0));
            const auto state=status.value("state",std::string());
            const auto label=desktop::sessionStatus(cancel_ && state=="listening"?"analysis_finished":state);
            state_->setText(QCoreApplication::translate("MonitorWindow", "%1 · Analyzed %2 / %3 I/Q pairs · Analysis discarded %4").arg(label).arg(qulonglong(status.value("processed_pairs",std::uint64_t(0)))).arg(qulonglong(status.value("source_pairs",std::uint64_t(0)))).arg(qulonglong(dropped)));
            const auto recording_lag=status.value("recording_lag_seconds",0.0);
            const auto analysis_lag=status.value("analysis_lag_seconds",0.0);
            if(analysis_lag>.2)state_->setText(state_->text()+QCoreApplication::translate("MonitorWindow", " · Analysis lag %1 s").arg(desktop::displayDecimal(analysis_lag,2)));
            if(recording_lag>.2)state_->setText(state_->text()+QCoreApplication::translate("MonitorWindow", " · Recording lag %1 s").arg(desktop::displayDecimal(recording_lag,2)));
            const auto selected=menus_.selection();
            const auto snapshotSkipped=status.value("snapshot_analysis",Json::object()).value("skipped_busy",0ULL);
            if(snapshotSkipped)state_->setText(state_->text()+QCoreApplication::translate("MonitorWindow"," · Busy snapshots skipped %1").arg(qulonglong(snapshotSkipped)));
            if(selected.snapshot.any() && !status.value("extended_available",false))
                state_->setText(state_->text()+QCoreApplication::translate("MonitorWindow", " · Some snapshot detectors unavailable"));
            const auto narrowband=desktop::narrowbandStatus(status);
            state_->setText(state_->text()+narrowband.suffix);
            if(dropped || narrowband.degraded || snapshotSkipped)
                state_->setText(state_->text()+u'\n'+QCoreApplication::translate("MonitorWindow","Analysis has gaps; detections may be missed."));
            else if(analysis_lag>.5)
                state_->setText(state_->text()+u'\n'+QCoreApplication::translate("MonitorWindow","Analysis is behind capture; results are delayed."));
            if(const auto analysis=status.find("wideband_analysis");analysis!=status.end())
                state_->setText(state_->text()+QCoreApplication::translate("MonitorWindow"," · LoRa %1 channels at %2 MS/s")
                    .arg(analysis->value("channel_count",1u)).arg(analysis->value("analysis_rate_hz",0.)/1e6,0,'f',3));
            const auto reads=desktop::receiverReadSummary(status);
            state_->setToolTip(narrowband.detail.isEmpty()?reads:
                reads.isEmpty()?narrowband.detail:narrowband.detail+u'\n'+reads);
            if(status.contains("receiver_configuration")) {
                const auto applied=desktop::receiverAppliedSummary(status["receiver_configuration"].value("applied",Json()));
                if(!applied.isEmpty())configuration_feedback_->setText(applied);
            }
            const bool needsAttention=error || state=="error" || state=="source_error" || dropped ||
                narrowband.degraded || snapshotSkipped || analysis_lag>.5 || recording_lag>.5;
            state_->setStyleSheet(needsAttention?"color:#ffb06c":"color:#9ee0c8");
            display_events_=status.value("recent_lora_events",Json::array());
            for(const auto& event:status.value("recent_fm_events",Json::array()))display_events_.push_back(event);
            for(const auto& event:status.value("recent_fsk_events",Json::array()))display_events_.push_back(event);
            for(const auto& event:status.value("recent_ask_events",Json::array()))display_events_.push_back(event);
            for(const auto& event:extended_.events())display_events_.push_back(event);
            for(const auto& candidate:status.value("recent_candidates",Json::array()))display_events_.push_back(candidate);
            events_->setUpdatesEnabled(false);
            events_->setRowCount(static_cast<int>(display_events_.size()));
            for(std::size_t row=0;row<display_events_.size();++row) {
                const auto cells=desktop::eventCells(display_events_[row]);
                for(int col=0;col<cells.size();++col) {
                    auto* item=events_->item(static_cast<int>(row),col);
                    if(!item)events_->setItem(static_cast<int>(row),col,new QTableWidgetItem(cells[col]));
                    else if(item->text()!=cells[col])item->setText(cells[col]);
                }
            }
            events_->setUpdatesEnabled(true);
            if(status.contains("storage")) {
                const auto& s=status["storage"];storage_->setText(QCoreApplication::translate("MonitorWindow", "Buffer %1 MiB · Saved %2 · Expired %3 · Quota %4 MiB · Confirmed LoRa only").arg(s.value("ring_bytes",0ULL)/1048576.0,0,'f',1).arg(qulonglong(s.value("completed",0ULL))).arg(qulonglong(s.value("retention_deleted",0ULL))).arg(qulonglong(s.value("quota_bytes",0ULL)/1048576)));
                if(status.contains("rss_bytes") && status.contains("cpu_percent_of_machine"))
                    storage_->setText(storage_->text()+QCoreApplication::translate("MonitorWindow", " · Process %1 MiB · CPU %2% of machine · %3 / %4 cores")
                        .arg(desktop::displayDecimal(status["rss_bytes"].get<double>()/1048576,0),
                             desktop::displayDecimal(status["cpu_percent_of_machine"].get<double>(),0),
                             desktop::displayDecimal(status.value("cpu_cores_used",0.),2),
                             QString::number(status.value("logical_processors",0u))));
                const auto completed=s.value("completed",0ULL);if(completed!=last_completed_){last_completed_=completed;refreshFiles();}
            }
        }
        if(error && running_) {
            state_->setStyleSheet("color:#ffb06c");
            try {std::rethrow_exception(error);}
            catch(const std::exception& e){
                desktop::show_error(*state_,e,QCoreApplication::translate("MonitorWindow", "The session stopped due to an error."));
                const auto reads=desktop::receiverReadSummary(status);
                if(!reads.isEmpty())state_->setToolTip(state_->toolTip()+"<pre>"+reads.toHtmlEscaped()+"</pre>");
            }
        }
        if(done&&running_){
            running_=false;
            live_tools_->setSession(false);
            worker_.join();
            if(closePending_) {
                if(!exportJob_.busy())close();
                return;
            }
            menus_.setSessionRunning(false);receiver_settings_->setEnabled(true);start_->setEnabled(true);
            stop_->setEnabled(false);pause_->setEnabled(false);frequency_->setEnabled(true);search_width_->setEnabled(true);gain_->setEnabled(true);
            quota_->setEnabled(true);limit_->setEnabled(true);refreshFiles();
        }
    }
    QString output_,input_,sourceText_;
    Config receiverConfiguration() const {
        Config cfg;cfg.rate=sample_rate_hz_;cfg.seconds=0;cfg.frequency=static_cast<std::uint64_t>(std::llround(frequency_->value()*1e6));cfg.device=selection_.index;cfg.device_serial=selection_.serial.toStdString();cfg.antenna=selection_.antenna.toStdString();
        cfg.input_mode=selection_.inputMode.toStdString();cfg.if_bandwidth_hz=selection_.ifBandwidth;cfg.receiver_settings=selection_.settings;
        const auto gain=gain_->currentData().toString();cfg.auto_gain=gain=="auto";if(!cfg.auto_gain)cfg.gain_tenths=static_cast<int>(std::lround(gain.toDouble()*10));return cfg;
    }
    std::shared_ptr<ReceiverSession> receiverSession(const Config& cfg) {
        if(auto existing=findReceiverSession(selection_.backend.toStdString(),cfg.device,cfg.device_serial))return existing;
        const auto key=selection_.backend.toStdString()+":"+(cfg.device_serial.empty()?std::to_string(cfg.device):cfg.device_serial);
        auto& result=receiverSessions_[key];if(!result)result=std::make_shared<ReceiverSession>(*receiver_,cfg,selection_.capabilities);return result;
    }
    std::shared_ptr<ReceiverSession> findReceiverSession(const std::string& backend,std::uint32_t index,const std::string& serial) const {
        for(const auto& [key,session]:receiverSessions_) {const auto identity=session->snapshot().value("identity",Json::object());const auto savedSerial=identity.value("serial",std::string());if(identity.value("backend",std::string())==backend && (!serial.empty() && !savedSerial.empty()?serial==savedSerial:identity.value("device",UINT32_MAX)==index))return session;}return {};
    }
    bool receptionBusy() const {for(const auto& [id,session]:receiverSessions_){const auto s=session->snapshot();const auto state=s.value("state",std::string());if(state=="receiving" || state=="starting" || state=="stopping")return true;}return false;}
    void disconnectReceiver(){if(running_ && input_.isEmpty())requestStop();for(auto& [id,session]:receiverSessions_)session->disconnect();receiverSessions_.clear();}
    void updateRuntimeStatus() {
        if(!runtimeStatus_)return;
        auto* bar=runtimeStatus_;const auto now=std::chrono::steady_clock::now();const auto elapsed=std::chrono::duration<double>(now-loadTime_).count();
        if(elapsed>=1) {const auto metrics=readProcessMetrics();double cores=0;if(metrics.cpu_seconds && lastCpu_)cores=std::max(0.,(*metrics.cpu_seconds-*lastCpu_)/elapsed);lastCpu_=metrics.cpu_seconds;loadTime_=now;
            bar->load->setText(bar->text("load",{{"cpu",desktop::displayDecimal(cores/std::max(1u,std::thread::hardware_concurrency())*100,0).toStdString()},{"cores",desktop::displayDecimal(cores,2).toStdString()},{"memory",metrics.resident_bytes?*metrics.resident_bytes/1048576:0}}));bar->load->setToolTip(bar->load->text());}
        Json sessions=Json::array();bool connected=false,transitioning=false;QStringList devices;
        for(const auto& [id,session]:receiverSessions_){auto value=session->snapshot();const auto state=value.value("state",std::string());connected|=state=="receiving";transitioning|=state=="starting" || state=="stopping";value["device_key"]=id;
            if(state=="receiving" || state=="starting" || state=="stopping"){const auto identity=value.at("identity");devices<<bar->text("device_identity",{{"name",identity.value("model",identity.value("backend",std::string()))},{"index",identity.value("device",0u)}});}sessions.push_back(std::move(value));}
        const auto device=devices.empty()?selection_.backend:devices.join(QStringLiteral(" · "));
        bar->device->setText(bar->text(connected?"connected":transitioning?"connecting":"idle",{{"device",device.toStdString()}}));bar->device->setToolTip(QString::fromUtf8(sessions.dump(2)));
        QStringList tasks;if(running_)tasks<<bar->text(input_.isEmpty()?"monitoring":"replay");const auto live=live_tools_->summary();if(!live.isEmpty())tasks<<live;bar->tasks->setText(tasks.empty()?bar->text("no_tasks"):tasks.join(QStringLiteral(" · ")));bar->tasks->setToolTip(bar->device->text()+u'\n'+bar->tasks->text()+u'\n'+bar->load->text());
        const auto dropped=rendered_status_.value("analysis_dropped_pairs",0ULL);if(dropped>lastDrops_)lastDropTime_=now;lastDrops_=dropped;const bool recent=running_ && dropped && now-lastDropTime_<std::chrono::seconds(3);
        const auto toolHealth=live_tools_->health();bool monitorFailure=false;{std::lock_guard lock(shared_.mutex);monitorFailure=bool(shared_.error);}
        bar->health->setText(!toolHealth.isEmpty()?toolHealth:monitorFailure?bar->text("monitor_error"):bar->text(recent?"analysis_active":dropped?"analysis_history":"healthy",{{"pairs",dropped}}));bar->health->setStyleSheet(recent || monitorFailure || !toolHealth.isEmpty()?QStringLiteral("color:#ffb06c"):QString());bar->health->setToolTip(toolHealth+u'\n'+state_->text()+u'\n'+storage_->text()+u'\n'+bar->load->text());
    }
    desktop::RuntimeStatus* runtimeStatus_=nullptr;std::map<std::string,std::shared_ptr<ReceiverSession>> receiverSessions_;
    std::chrono::steady_clock::time_point loadTime_=std::chrono::steady_clock::now(),lastDropTime_{};std::optional<double> lastCpu_;std::uint64_t lastDrops_=0;
    QString replayEventRecording_;
    std::uint64_t replayEventPairs_=0;
    std::uint32_t replayEventRate_=0;
    desktop::ReceiverSelection selection_;
    const ReceiverBackend* receiver_;
    QPushButton* receiver_settings_;
    QDoubleSpinBox* frequency_;QComboBox* gain_;QSpinBox* quota_;
    desktop::SearchWidthControl* search_width_;
    QLabel* gain_label_;
    QDoubleSpinBox* limit_;
    QPushButton *start_,*stop_,*pause_;QLabel *source_=nullptr,*state_,*power_,*storage_,*coverage_=nullptr;
    std::uint32_t sample_rate_hz_=2000000;int detection_workers_=recommendedAnalysisWorkers();double threshold_db_=18;
    std::vector<desktop::SignalDisplay*> displays_;QTextEdit* evidence_;QTableWidget *events_,*files_;AnalysisPanel* analysis_;
    desktop::Workspace* workspace_=nullptr;QWidget *controlsPane_=nullptr,*statusPane_=nullptr;
    QTimer timer_;Shared shared_;std::jthread worker_;std::atomic<bool> cancel_=false,paused_=false;
    QProcess capabilityQuery_;QTimer capabilityDeadline_;QLabel* configuration_feedback_=nullptr;
    bool running_=false,diagnostics_=false;Json display_events_=Json::array();std::uint64_t last_completed_=0;
    Json rendered_status_=Json::object();std::uint64_t rendered_spectrum_sequence_=0;
    SnapshotAnalysis extended_;
    std::unique_ptr<desktop::LiveTools> live_tools_;
    QPushButton* export_file_=nullptr;
    bool closePending_=false;
    desktop::RecordingExportJob exportJob_;
    desktop::ToolboxCenter* toolboxes_=nullptr;
    desktop::DesktopMenus menus_;
};
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setApplicationName("SignalScope");app.setApplicationDisplayName("Foreshadow");app.setOrganizationName("SignalScope");
    desktop::applyDesktopTheme(app);
    const auto args=app.arguments();auto option=[&](const QString& name){auto at=args.indexOf(name);return at>=0&&at+1<args.size()?args[at+1]:QString();};
    if(args.contains("--settings-dir")) {
        const auto directory=option("--settings-dir");
        if(directory.isEmpty() || directory.startsWith("--") || !QDir().mkpath(directory) ||
           !QFileInfo(directory).isWritable()) {
            std::cerr<<"Use --settings-dir with a writable directory.\n";
            return 2;
        }
        const auto absolute=QDir(directory).absolutePath();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,absolute);
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,QDir(absolute).filePath("system"));
        if(!qEnvironmentVariableIsSet("SIGNALSCOPE_DISTRIBUTION_ROOT"))qputenv("SIGNALSCOPE_DISTRIBUTION_ROOT",QDir(absolute).filePath("distribution").toUtf8());
    }
    desktop::Language language;
    if((args.contains("--language") && option("--language").isEmpty()) || !language.install(option("--language"))) {
        std::cerr<<"Could not load the requested language. Use --language system or an installed language tag.\n";
        return 2;
    }
    QString output=option("--output");if(output.isEmpty())output=QDir::homePath()+"/SignalScope-events";
    QSettings settings;
    std::shared_ptr<distribution::Service> updates;
    try{updates=desktop::makeDistributionService();}
    catch(const std::exception& error){QMessageBox::critical(nullptr,desktop::toolboxText("title"),desktop::toolboxError(error));return 1;}
    const bool explicitReceiver=args.contains("--receiver");
    const auto& fallbackReceiver=default_receiver();
    const auto receiverName=explicitReceiver?option("--receiver"):settings.value("desktop/receiver_backend",QString::fromLatin1(fallbackReceiver.id)).toString();
    const auto* receiver=find_receiver(receiverName.toStdString());
    if(!receiver && (!explicitReceiver || args.contains("--update-health-request")))receiver=&fallbackReceiver;
    if(!receiver) {
        std::cerr<<"Unknown receiver backend. See signalscope --help.\n";
        return 2;
    }
    const auto prefix=desktop::receiverSettingsPrefix(QString::fromLatin1(receiver->id));
    bool device_ok=true;
    const auto device=args.contains("--device")?option("--device").toUInt(&device_ok):settings.value(prefix+"device",0).toUInt();
    if(!device_ok || device>1000) {
        std::cerr<<"Use a device index from 0 to 1000.\n";
        return 2;
    }
    const auto antenna=args.contains("--antenna")?option("--antenna"):
        args.contains("--device")?QString():settings.value(prefix+"antenna").toString();
    auto pluginDirectory=option("--research-plugin-dir");
    if(pluginDirectory.isEmpty()) {
        const auto appData=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if(!appData.isEmpty())pluginDirectory=QDir(appData).filePath("research-plugins");
    }
    desktop::ReceiverSelection selection{QString::fromLatin1(receiver->id),device,antenna};
    if(!args.contains("--device"))selection.serial=settings.value(prefix+"serial").toString();
    Window window(output,option("--replay"),std::move(selection),pluginDirectory,language.tag(),std::move(updates));
    if(args.contains("--verify-live-tools")) {
        const auto diagnostics=window.liveToolDiagnostics();std::cout<<Json{{"protocol_version",desktop::liveResearchProtocolVersion},{"providers",diagnostics}}.dump()<<'\n';
        return std::all_of(diagnostics.begin(),diagnostics.end(),[](const auto& item){return item.at("available").template get<bool>();})?0:1;
    }
    if(args.contains("--analysis-plugin") && !window.selectAnalysisPlot(option("--analysis-plugin"))) {
        std::cerr<<"Unknown analysis plugin.\n";
        return 2;
    }
    language.add_menu(window);
    if(!window.setDsp(option("--rate"),option("--workers"),option("--threshold-db"))){
        std::cerr<<"Rate must be 1–100000000 S/s; device and analysis limits are checked separately. Recommended presets:";
        for(const auto rate:receiver->monitor_rates)std::cerr<<' '<<rate;
        std::cerr<<". Workers: 1–32. Threshold: 6–60 dB.\n";
        return 2;
    }
    if(args.contains("--diagnostic"))window.enableDiagnostics();
    if(!option("--gain").isEmpty()&&!window.setGain(option("--gain"))){std::cerr<<"Unsupported gain setting.\n";return 2;}
    const auto frequency=option("--frequency");
    if(!frequency.isEmpty()) {
        bool ok=false;const auto mhz=frequency.toDouble(&ok);
        if(!ok || !window.setFrequency(mhz)){
            std::cerr<<"Frequency is outside the selected receiver's tuning range.\n";
            return 2;
        }
    }
    if(args.contains("--search-width")) {
        bool ok=false;
        const auto hz=option("--search-width").toUInt(&ok);
        if(!ok || !window.setSearchWidth(hz)) {
            std::cerr<<"Search width must be from 1000 to 100000000 Hz in 1000 Hz steps; device and analysis compatibility are checked separately.\n";
            return 2;
        }
    }
    if(args.contains("--smoke-compact"))window.compactWorkspace();
    if(args.contains("--smoke-wide"))window.wideWorkspace();
    window.show();
    const auto researchLayout=option("--smoke-research-layout");
    const auto restoredResearchLayout=option("--smoke-restored-research-layout");
    if(!researchLayout.isEmpty()) {
        window.setWindowState(Qt::WindowNoState);window.resize(1600,900);
        QTimer::singleShot(0,&window,[&window,researchLayout]{window.selectWorkspaceLayout(researchLayout);});
    }
    try{desktop::confirmDistributionStartup(args);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    if(args.contains("--toolbox-center"))window.openToolboxes();
    if(args.contains("--check-for-updates"))QTimer::singleShot(100,&window,[&]{window.checkUpdates();});
    else if(!args.contains("--smoke-seconds") && settings.value("updates/check_on_startup",true).toBool())QTimer::singleShot(1500,&window,[&]{window.checkUpdates(true);});
    const auto analyze=option("--analyze");if(!analyze.isEmpty())QTimer::singleShot(100,&window,[&]{window.analyzeFile(analyze);});
    const auto smoke=option("--smoke-seconds");
    if(args.contains("--smoke-stop-listening-after-ms")) {
        bool valid=false;const auto delay=option("--smoke-stop-listening-after-ms").toInt(&valid);
        if(smoke.isEmpty() || !valid || delay<=0){std::cerr<<window.runtimeText("smoke_stop_usage").toUtf8().constData()<<'\n';return 2;}
        QTimer::singleShot(delay,&window,[&]{window.stopListening();});
    }
    if(args.contains("--autostart"))QTimer::singleShot(100,&window,[&]{
        window.start();
        if(!smoke.isEmpty() && args.contains("--smoke-close-active") &&
           (!window.isSessionRunning() || window.close())) {
            std::cerr<<"The desktop did not defer closing the active session.\n";
            app.exit(4);
        }
    });
    const auto smokeProcessor=option("--smoke-analysis-processor");
    bool liveActionStarted=false;
    QTimer liveActionProbe;
    if(!smoke.isEmpty() && args.contains("--smoke-live-event")) {
        QFile file(option("--smoke-live-event"));
        if(!file.open(QIODevice::ReadOnly)){std::cerr<<"Could not open live event fixture.\n";return 4;}
        Json event;
        try{event=Json::parse(file.readAll().toStdString());}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 4;}
        QObject::connect(&liveActionProbe,&QTimer::timeout,&window,[&,event]{
            if(window.triggerLiveAction(event,option("--smoke-live-action"))){liveActionStarted=true;liveActionProbe.stop();}
        });
        liveActionProbe.start(100);
    }
    QTimer processorProbe;
    if(!smoke.isEmpty() && !smokeProcessor.isEmpty()) {
        QObject::connect(&processorProbe,&QTimer::timeout,&window,[&]{
            if(!window.hasAnalysisResult())return;
            processorProbe.stop();
            window.selectAnalysisPlot(smokeProcessor);
        });
        processorProbe.start(50);
    }
    const bool fmEventSmoke=args.contains("--smoke-research-fm-event");
    const bool eventSmoke=!smoke.isEmpty() && (args.contains("--smoke-research-event") || fmEventSmoke);
    bool eventResearchStarted=false;
    QTimer eventProbe;
    if(eventSmoke) {
        eventProbe.setSingleShot(true);
        QObject::connect(&eventProbe,&QTimer::timeout,&window,[&]{
            if(!eventResearchStarted && window.replayCompleted())
                eventResearchStarted=window.smokeResearchEvent(fmEventSmoke);
            if(eventResearchStarted && window.hasAnalysisResult()) {
                if(!args.contains("--screenshot") && !args.contains("--smoke-compact"))app.quit();
            } else eventProbe.start(100);
        });
        eventProbe.start(100);
    }
    if(!smoke.isEmpty() && args.contains("--smoke-compact"))window.resize(640,480);
    if(!smoke.isEmpty()) {
        if(args.contains("--smoke-modulation-heading") &&
           window.eventModulationHeading()!=option("--smoke-modulation-heading")) {
            std::cerr<<"The event table did not use the expected modulation heading.\n";
            return 4;
        }
        if(args.contains("--smoke-recording-time") &&
           !window.recordingTooltipContains(option("--smoke-recording-time"),option("--smoke-recording-time-heading"))) {
            std::cerr<<"The recording list did not show the expected timestamp and localized heading.\n";
            return 4;
        }
        if(args.contains("--smoke-companion")) {
            const auto expected=QFileInfo(option("--smoke-companion")).canonicalFilePath();
            const auto companion=desktop::companion_cli();
            if(expected.isEmpty() || QFileInfo(companion).canonicalFilePath()!=expected) {
                std::cerr<<"The desktop resolved a different companion executable.\n";
                return 4;
            }
            QProcess process;
            process.start(companion,{"--help"});
            if(!process.waitForStarted(3000) || !process.waitForFinished(5000) ||
               process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0 ||
               !process.readAllStandardOutput().contains("Available IDs:")) {
                std::cerr<<"The desktop could not run the companion executable.\n";
                return 4;
            }
        }
        QFileDialog picker;
        picker.setOption(QFileDialog::DontUseNativeDialog);
        picker.setFileMode(QFileDialog::ExistingFile);
        const auto open=picker.labelText(QFileDialog::Accept).remove(u'&');
        if(open.isEmpty() || (open=="Open")!=(language.tag()=="en")) {
            std::cerr<<"The file picker did not use the selected desktop language.\n";
            return 4;
        }
    }
    const auto screenshot=option("--screenshot");
    if(!screenshot.isEmpty())QTimer::singleShot(option("--screenshot-delay-ms").isEmpty()?1500:option("--screenshot-delay-ms").toInt(),&window,[&]{window.grab().save(screenshot);if(smoke.isEmpty()&&!args.contains("--autostart"))app.quit();});
    if(!smoke.isEmpty())QTimer::singleShot(smoke.toInt()*1000,&app,[&]{
        if(args.contains("--smoke-stop-listening-after-ms") && window.isSessionRunning()){std::cerr<<window.runtimeText("smoke_stop_unfinished").toUtf8().constData()<<'\n';app.exit(4);return;}
        if(args.contains("--smoke-live-event") && !liveActionStarted){std::cerr<<"The live event action did not become available.\n";app.exit(4);return;}
        if(args.contains("--smoke-export-live-status")) {
            QFile output(option("--smoke-export-live-status"));
            const auto bytes=QByteArray::fromStdString(window.liveToolDiagnostics().dump(2));
            if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size()) {
                std::cerr<<"The desktop did not save live tool diagnostics.\n";
                app.exit(4);return;
            }
        }
        if(args.contains("--smoke-export-session-status")) {
            QFile output(option("--smoke-export-session-status"));
            const auto bytes=QByteArray::fromStdString(window.sessionDiagnostics().dump(2));
            if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size()) {
                std::cerr<<"The desktop did not save session diagnostics.\n";
                app.exit(4);return;
            }
        }
        if(args.contains("--smoke-wide") && !window.wideWorkspaceFits()) {
            std::cerr<<"The large workspace did not expose enough panels or clipped session controls.\n";
            app.exit(3);return;
        }
        if(!researchLayout.isEmpty() && (!window.researchWorkspaceFits(researchLayout) || (args.contains("--smoke-compact") && window.width()!=640))) {
            std::cerr<<window.runtimeText("smoke_workspace_layout").toUtf8().constData()<<'\n';app.exit(4);return;
        }
        if(!restoredResearchLayout.isEmpty() && !window.researchWorkspaceFits(restoredResearchLayout,true)) {
            std::cerr<<window.runtimeText("smoke_workspace_layout").toUtf8().constData()<<'\n';app.exit(4);return;
        }
        if(args.contains("--smoke-compact") && researchLayout.isEmpty() && restoredResearchLayout.isEmpty()) {
            const bool compactFits=window.width()==640 && window.sessionControlsFit() && window.eventLayoutFits(true);
            window.resize(1120,680);
            window.monitorWorkspace();QCoreApplication::processEvents();
            if(!compactFits || !window.eventLayoutFits(false)) {
                std::cerr<<"The session controls or event panel did not fit the desktop layout.\n";
                app.exit(4);
                return;
            }
        }
        if(args.contains("--smoke-replay-narrowband") && !window.smokeReplayNarrowband()) {
            std::cerr<<"The desktop did not identify FSK, OOK and ASK in the replay.\n";
            app.exit(3);
        } else if(eventSmoke && (!eventResearchStarted || !window.hasAnalysisResult())) {
            std::cerr<<"The desktop did not complete research from a replay event.\n";
            app.exit(3);
        } else if(!analyze.isEmpty() && !window.hasAnalysisResult()) {
            std::cerr<<"The desktop did not load an analysis result.\n";
            app.exit(3);
        } else if(args.contains("--smoke-export-analysis") &&
                  !window.exportAnalysisResult(option("--smoke-export-analysis"))) {
            std::cerr<<"The desktop did not save the analysis result.\n";
            app.exit(4);
        } else if(args.contains("--smoke-export-previous-analysis") &&
                  (!window.restorePreviousAnalysis() ||
                   !window.exportAnalysisResult(option("--smoke-export-previous-analysis")))) {
            std::cerr<<"The desktop did not restore and save the input artifact.\n";
            app.exit(4);
        } else if(args.contains("--autostart") && !option("--replay").isEmpty() && !window.replayCompleted()) {
            std::cerr<<"The desktop did not complete replay.\n";
            app.exit(3);
        } else app.quit();
    });
    return app.exec();
}
