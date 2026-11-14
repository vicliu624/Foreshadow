#include "plugins/desktop/research/waveform/laboratory.hpp"
#include "plugins/desktop/research/waveform/trace_view.hpp"
#include "plugins/desktop/research/waveform/research_views.hpp"
#include "live_tools.hpp"
#include "workspace.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "test_process.hpp"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTabWidget>
#include <QPushButton>
#include <iostream>
#include <numbers>
#include <thread>
#include <source_location>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void until(F predicate,std::source_location location=std::source_location::current(),std::chrono::seconds timeout=std::chrono::seconds(5)) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<deadline){QCoreApplication::processEvents();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error("Waveform laboratory did not reach its expected observable state at test line "+std::to_string(location.line()));
}
void chooseFile(QAction* action,const std::filesystem::path& path) {
    require(action && action->isEnabled(),"Expected a usable analysis file action");
    bool selected=false,expired=false;QTimer timer;timer.setInterval(20);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    QObject::connect(&timer,&QTimer::timeout,[&]{
        if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            if(std::chrono::steady_clock::now()>deadline){expired=true;dialog->reject();return;}
            // Drive the editable filename field, as a user would. selectFile()
            // alone need not replace text in an already focused visible dialog.
            auto* name=dialog->findChild<QLineEdit*>("fileNameEdit");
            require(name,"The nonnative file dialog has no filename entry");
            name->setText(QString::fromStdString(path_to_utf8(path)));selected=true;
            QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
        }
    });
    timer.start();action->trigger();timer.stop();require(selected && !expired,"The analysis dialog did not select its native path");
}
// Opt-in review of a private recording. It never runs in the ordinary CTest
// suite, opens no receiver, and writes only to an exclusively new review folder.
void reviewRecording(const std::filesystem::path& metadata,const std::filesystem::path& profilePath,const std::filesystem::path& output) {
    std::ifstream profileFile(profilePath);Json profile;profileFile>>profile;
    require(std::filesystem::create_directory(output),"The recording review requires a new output directory");
    QMainWindow window;Workspace workspace(window);LiveTools host({waveformLaboratoryProvider()},"zh-CN",&window);
    for(const auto& panel:host.panels()){workspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);if(panel.relativeTo)workspace.place(panel.widget,panel.relativeTo,panel.orientation);}
    host.showPanel=[&](auto* panel){workspace.show(panel);};workspace.restore([](bool){});window.setWindowState(Qt::WindowNoState);window.resize(1600,1000);window.show();QCoreApplication::processEvents();
    const auto metadataBefore=amc_test::read(QString::fromStdString(path_to_utf8(metadata)));
    const auto first=profile.at("file_first_sample").get<std::uint64_t>(),count=profile.at("file_sample_count").get<std::uint64_t>();
    QMenu menu;host.addRecordingActions(menu,{metadata,ResearchSampleRange{first,4096},{{"review","passive-recording; no controlled transmitter truth"}}});require(menu.actions().size()==1,"The recording has no research action");menu.actions().front()->trigger();
    auto state=[&]{return host.diagnostics()[0].at("status");};until([&]{return state().at("state")=="ready";});
    const auto* widget=host.panels()[0].widget;auto* inspector=host.panels()[1].widget;
    inspector->findChild<QCheckBox*>("waveform-channel-enabled")->setChecked(true);
    inspector->findChild<QDoubleSpinBox*>("waveform-channel-offset")->setValue(profile.at("offset_hz").get<double>()/1000);
    inspector->findChild<QDoubleSpinBox*>("waveform-channel-passband")->setValue(profile.at("passband_hz").get<double>()/1000);
    inspector->findChild<QDoubleSpinBox*>("waveform-channel-transition")->setValue(profile.at("transition_hz").get<double>()/1000);
    inspector->findChild<QSpinBox*>("waveform-channel-rate")->setValue(profile.at("output_rate_hz").get<int>());
    inspector->findChild<QCheckBox*>("waveform-channel-file-scope")->setChecked(true);
    inspector->findChild<QLineEdit*>("waveform-channel-file-first")->setText(QString::number(first));inspector->findChild<QLineEdit*>("waveform-channel-file-count")->setText(QString::number(count));
    inspector->findChild<QPushButton*>("waveform-channel-apply")->click();
    until([&]{const auto value=state();if(value.at("state")=="failed")throw std::runtime_error("Recorded channel review failed: "+value.dump());return value.at("state")=="ready" && value.at("channel").contains("samples");},std::source_location::current(),std::chrono::seconds(60));
    require(state().at("loaded_samples")==4096 && state().at("channel").at("method").at("output_rate_hz")==profile.at("output_rate_hz"),"A real long input replaced the preview or changed the requested output clock");
    auto* processed=dynamic_cast<TraceView*>(widget->findChild<QWidget*>("waveform-channel-track-0"));require(processed,"The processed tracks are missing");
    QKeyEvent full(QEvent::KeyPress,Qt::Key_Home,Qt::NoModifier);QApplication::sendEvent(processed,&full);
    widget->findChild<QTabWidget*>("waveform-track-tabs")->setCurrentIndex(1);inspector->findChild<QTabWidget*>()->setCurrentIndex(2);
    QCoreApplication::processEvents();const QPointF selectedPoint(processed->width()*.7,processed->height()*.5);QMouseEvent pick(QEvent::MouseButtonPress,selectedPoint,QPointF(processed->mapToGlobal(selectedPoint.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(processed,&pick);
    QCoreApplication::processEvents();require(window.grab().save(QString::fromStdString(path_to_utf8(output/"channel.png"))),"Could not render the real channel");
    inspector->findChild<QCheckBox*>("waveform-window-enabled")->setChecked(true);inspector->findChild<QComboBox*>("waveform-window-input")->setCurrentIndex(1);
    auto* fft=inspector->findChild<QComboBox*>("waveform-window-fft");fft->setCurrentIndex(fft->findData(profile.at("fft_size").get<unsigned>()));inspector->findChild<QSpinBox*>("waveform-window-hop")->setValue(profile.at("hop_samples").get<int>());
    inspector->findChild<QPushButton*>("waveform-window-apply")->click();until([&]{return state().at("state")=="ready" && state().at("window").contains("observed_samples");},std::source_location::current(),std::chrono::seconds(60));
    widget->findChild<QTabWidget*>("waveform-track-tabs")->setCurrentIndex(2);inspector->findChild<QTabWidget*>()->setCurrentIndex(3);
    QCoreApplication::processEvents();require(window.grab().save(QString::fromStdString(path_to_utf8(output/"window.png"))),"Could not render the real window research");
    const auto analysis=output/"review.amc-analysis.json";chooseFile(widget->findChild<QAction*>("waveform-save-analysis"),analysis);until([&]{return state().at("last_save").value("committed",false);});
    const auto before=state();chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),analysis);until([&]{return state().at("state")=="ready";},std::source_location::current(),std::chrono::seconds(60));
    require(state().at("configuration").at("channel")==before.at("configuration").at("channel") && state().at("window").at("observed_samples")==before.at("window").at("observed_samples"),"The real input did not reproduce its applied range and window");
    widget->findChild<QTabWidget*>("waveform-track-tabs")->setCurrentIndex(1);widget->findChild<QPushButton*>("waveform-channel-reveal")->click();until([&]{return state().at("state")=="ready" && state().at("loaded_samples").get<unsigned>()>4096;},std::source_location::current(),std::chrono::seconds(60));
    QCoreApplication::processEvents();require(window.grab().save(QString::fromStdString(path_to_utf8(output/"original.png"))),"Could not render the located original observations");
    require(amc_test::read(QString::fromStdString(path_to_utf8(metadata)))==metadataBefore,"The recording review modified source metadata");
    write_json(output/"result.json",{{"scope","existing passive recording; software workflow only; no decode or transmitter attribution"},{"profile",profile},{"before_original_location",before},{"after_original_location",state()}});
    std::cout<<"Recorded source preview, bounded long channel, window research, save/reopen and original location passed: "<<path_to_utf8(output)<<'\n';
}
void workflow(const std::filesystem::path& root,const QString& screenshot) {
    const auto capture=root/path_from_utf8("实测形状 📡");Config config;config.rate=2'000'000;config.frequency=433000000;
    const auto format=SampleFormat::cf32_le;const auto stride=bytes_per_pair(format);Bytes bytes(16384*stride);
    for(std::size_t n=0;n<16384;++n) {
        const auto t=double(n%8192)/config.rate,absolute=double(n)/config.rate;
        const auto value=std::polar(.58,.27+2*std::numbers::pi*(-62500*t+.5*(125000/.004096)*t*t+1200*absolute));
        write_iq_pair(bytes.data()+n*stride,format,{float(value.real()),float(value.imag())});
    }
    constexpr std::uint64_t origin=(1ULL<<54)+123;
    Recorder recording(capture,config,"independent-ui-truth",format);recording.append(bytes);recording.finish("complete",{{"stream_first_sample",origin}});
    const auto metadata=capture/"capture.sigmf-meta";
    QMainWindow window;window.resize(1280,900);Workspace workspace(window);
    LiveTools host({waveformLaboratoryProvider()},"zh-CN",&window);
    for(const auto& panel:host.panels()){workspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);if(panel.relativeTo)workspace.place(panel.widget,panel.relativeTo,panel.orientation);workspace.setVisible(panel.widget,false);}
    for(const auto& preset:host.layouts())workspace.addLayout(preset.id,preset.title,[&workspace,preset]{
        std::vector<WorkspaceLayoutItem> items{{preset.primary,Qt::RightDockWidgetArea}};
        for(const auto& panel:preset.panels)items.push_back({panel.widget,panel.preferredArea,panel.relativeTo,panel.orientation});workspace.applyLayout(items);
    });
    host.showPanel=[&](auto* panel){workspace.show(panel);};workspace.restore([](bool){});if(!screenshot.isEmpty())window.resize(1280,900);window.show();QCoreApplication::processEvents();
    auto* preset=window.findChild<QAction*>("workspace-layout/"+host.layouts().at(0).id);require(preset,"The actual research preset was not available");preset->trigger();
    const Json event={{"modulation_id","independent.unknown.signal"},{"features",{{"unrecognized_field",23}}}};
    QMenu menu;host.addRecordingActions(menu,{metadata,ResearchSampleRange{0,16384},event});require(menu.actions().size()==1,"Unknown recorded signal lacked a waveform entry");menu.actions().front()->trigger();
    auto state=[&]{return host.diagnostics()[0].value("status",Json::object());};
    until([&]{return state().value("state",std::string())=="ready";});
    require(state().at("loaded_samples")==16384,"The visible window lost original samples");
    auto* widget=host.panels()[0].widget;
    auto* scale=widget->findChild<QComboBox*>("waveform-scale");require(scale,"Time scale control is missing");
    const auto index=scale->findData(.000032);require(index>=0,"Sample-scale preset is missing");scale->setCurrentIndex(index);QMetaObject::invokeMethod(scale,"activated",Qt::DirectConnection,Q_ARG(int,index));
    require(state().at("configuration").at("view").at("count")=="64","The 32 microsecond view did not map to 64 real samples");
    const auto before=std::stoull(state().at("configuration").at("view").at("cursor_b").get<std::string>());
    auto* track=dynamic_cast<TraceView*>(widget->findChild<QWidget*>("waveform-track-0"));require(track,"I/Q trace is missing");
    QKeyEvent key(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);QApplication::sendEvent(track,&key);
    require(state().at("configuration").at("view").at("cursor_b")==std::to_string(before+1),"A linked cursor lost source precision");
    widget->findChild<QComboBox*>("waveform-phase-mode")->setCurrentIndex(1);widget->findChild<QCheckBox*>("waveform-show-smoothed")->setChecked(true);
    const auto inspector=host.panels()[1].widget;
    require(inspector->findChild<QPlainTextEdit*>("waveform-provenance")->toPlainText().contains("independent.unknown.signal"),"Unknown event fields were not available for inspection");
    require(inspector->findChild<QLabel*>("sample.index")->text()==QString::number(before+1),"The inspector does not follow the linked source cursor");
    inspector->findChild<QDoubleSpinBox*>("waveform-amplitude-floor")->setValue(.2);
    const auto saved=root/path_from_utf8("分析恢复 📡.amc-analysis.json");chooseFile(widget->findChild<QAction*>("waveform-save-analysis"),saved);
    until([&]{return state().at("last_save").value("committed",false);});
    std::ifstream file(saved);Json configFile;file>>configFile;
    require(configFile.at("parameters").at("relative_amplitude_floor")==.05,"Saving recorded draft parameters as applied parameters");
    require(configFile.at("view").at("count")=="64" && configFile.at("event")==event,"Saved view or event context was lost");
    QKeyEvent reset(QEvent::KeyPress,Qt::Key_Home,Qt::NoModifier);QApplication::sendEvent(track,&reset);
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),saved);until([&]{return state().value("state",std::string())=="ready";});
    require(state().at("configuration").at("view")==configFile.at("view"),"Reopening an analysis failed to reproduce the selected view and cursors");
    auto legacy=configFile;legacy["version"]=1;legacy["source"].erase("path_base");legacy["source"]["metadata_path"]=path_to_utf8(metadata);const auto legacyPath=root/"legacy.amc-analysis.json";write_json(legacyPath,legacy);
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),legacyPath);until([&]{return state().value("state",std::string())=="ready";});
    require(state().at("configuration").at("view")==configFile.at("view"),"Version 1 source references no longer reproduce the saved view");
    require(!widget->visibleRegion().isEmpty() && !inspector->visibleRegion().isEmpty(),"Main research and inspector could not be viewed together");
    if(!screenshot.isEmpty()){QCoreApplication::processEvents();require(window.grab().save(screenshot),"Could not render the native laboratory screenshot");}
    auto* channelEnabled=inspector->findChild<QCheckBox*>("waveform-channel-enabled");channelEnabled->setChecked(true);
    inspector->findChild<QDoubleSpinBox*>("waveform-channel-offset")->setValue(12.5);inspector->findChild<QDoubleSpinBox*>("waveform-channel-passband")->setValue(200);
    inspector->findChild<QDoubleSpinBox*>("waveform-channel-transition")->setValue(100);inspector->findChild<QSpinBox*>("waveform-channel-rate")->setValue(500001);
    inspector->findChild<QPushButton*>("waveform-channel-apply")->click();until([&]{return state().at("state")=="ready" && state().at("channel").contains("samples");});
    require(state().at("loaded_samples")==16384 && state().at("channel").at("samples").get<unsigned>()<5000 && state().at("channel").at("method").at("output_rate_hz")==500001,"Channel UI replaced the original observations or lost its actual output clock");
    auto* processed=dynamic_cast<TraceView*>(widget->findChild<QWidget*>("waveform-channel-track-0"));require(processed,"Processed I/Q view is missing");
    const auto channelFirst=std::stoull(state().at("channel").at("configuration").at("view").at("first").get<std::string>());processed->setRange({channelFirst+200,100});
    require(std::stoull(state().at("configuration").at("view").at("count").get<std::string>())>100,"Processed zoom did not locate the shared original source interval");
    QKeyEvent channelKey(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);QApplication::sendEvent(processed,&channelKey);
    require(widget->findChild<QLabel*>("waveform-channel-cursor")->text().contains("/"),"A fractional processed point was silently rounded to an original observation");
    widget->findChild<QTabWidget*>("waveform-track-tabs")->setCurrentIndex(1);
    const auto processedConfig=root/path_from_utf8("信道复现 📡.amc-analysis.json");chooseFile(widget->findChild<QAction*>("waveform-save-analysis"),processedConfig);
    until([&]{return state().at("last_save").value("committed",false);});std::ifstream processedFile(processedConfig);Json processedJson;processedFile>>processedJson;
    require(processedJson.at("version")==3 && processedJson.at("channel").at("output_rate_hz")==500001,"A processed result was saved using the raw-only configuration protocol");
    channelEnabled->setChecked(false);inspector->findChild<QPushButton*>("waveform-channel-apply")->click();until([&]{return state().at("state")=="ready" && !state().at("channel").contains("samples");});
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),processedConfig);until([&]{return state().at("state")=="ready" && state().at("channel").contains("samples");});
    require(state().at("configuration").at("channel")==processedJson.at("channel") && widget->findChild<QTabWidget*>("waveform-track-tabs")->currentIndex()==1,"Reopening the processing chain did not reproduce applied parameters, selection and rational cursors");
    const auto beforeRate=state().at("channel").at("configuration");const auto oldCursor=std::stoull(beforeRate.at("view").at("cursor_b").get<std::string>());
    inspector->findChild<QSpinBox*>("waveform-channel-rate")->setValue(400003);inspector->findChild<QPushButton*>("waveform-channel-apply")->click();
    until([&]{return state().at("state")=="ready" && state().at("channel").at("configuration").at("output_rate_hz")==400003;});
    require(state().at("channel").at("configuration").at("view").at("cursor_b")==std::to_string((oldCursor*400003+500000)/500001),"Changing sample rate preserved an output index instead of the exact source-time cursor");
    const auto changedCount=std::stoull(state().at("channel").at("configuration").at("view").at("count").get<std::string>());require(changedCount>=79 && changedCount<=82,"Changing output sample rate moved the selected physical time interval");
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),processedConfig);until([&]{return state().at("state")=="ready" && state().at("configuration").at("channel")==processedJson.at("channel");});
    if(!screenshot.isEmpty()){QCoreApplication::processEvents();require(window.grab().save(screenshot+".channel.png"),"Could not render processed channel workflow");}
    QKeyEvent fullRange(QEvent::KeyPress,Qt::Key_Home,Qt::NoModifier);QApplication::sendEvent(track,&fullRange);
    inspector->findChild<QCheckBox*>("waveform-window-enabled")->setChecked(true);inspector->findChild<QComboBox*>("waveform-window-input")->setCurrentIndex(1);
    auto* fft=inspector->findChild<QComboBox*>("waveform-window-fft");fft->setCurrentIndex(fft->findData(256));inspector->findChild<QSpinBox*>("waveform-window-hop")->setValue(128);
    inspector->findChild<QComboBox*>("waveform-window-function")->setCurrentIndex(2);inspector->findChild<QSpinBox*>("waveform-window-lags")->setValue(8);inspector->findChild<QSpinBox*>("waveform-window-bins")->setValue(16);
    inspector->findChild<QPushButton*>("waveform-window-apply")->click();until([&]{return state().at("state")=="ready" && state().at("window").contains("observed_samples");});
    require(state().at("window").at("configuration").at("processed")==true && state().at("window").at("method").at("window")=="hamming" && std::abs(state().at("window").at("mean_power").get<double>()-.3364)<.01,"Window research did not apply its selected source, complete configuration or normalized power");
    processed->setRange({channelFirst+200,1200});require(!widget->findChild<QAction*>("waveform-save-analysis")->isEnabled(),"Saving was enabled while the linked window selection was waiting for computation");until([&]{const auto s=state();return s.at("state")=="ready" && s.at("window").at("configuration").at("range").at("count")=="1200";});
    processed->setRange({channelFirst+200,100});until([&]{const auto s=state();return s.at("state")=="ready" && s.at("window").at("configuration").at("range").at("count")=="100";});
    auto* spectrum=dynamic_cast<ResearchCurveView*>(widget->findChild<QWidget*>("waveform-psd"));require(spectrum,"The average spectrum is missing");spectrum->grab();
    require(spectrum->renderedPrimitives()==0 && state().at("window").at("observed_samples")==100,"A selection without a complete FFT frame displayed invented zero spectral power");
    processed->setRange({channelFirst+200,1200});until([&]{const auto s=state();return s.at("state")=="ready" && s.at("window").at("configuration").at("range").at("count")=="1200";});
    widget->findChild<QTabWidget*>("waveform-track-tabs")->setCurrentIndex(2);inspector->findChild<QDoubleSpinBox*>("waveform-window-color-low")->setValue(-90);inspector->findChild<QDoubleSpinBox*>("waveform-window-color-high")->setValue(-25);
    if(!screenshot.isEmpty()){QCoreApplication::processEvents();require(window.grab().save(screenshot+".window.png"),"Could not render the actual time-frequency workflow");}
    fft->setCurrentIndex(fft->findData(512));const auto windowConfig=root/path_from_utf8("时频复现 📡.amc-analysis.json");chooseFile(widget->findChild<QAction*>("waveform-save-analysis"),windowConfig);
    until([&]{return state().at("last_save").value("committed",false);});std::ifstream windowFile(windowConfig);Json windowJson;windowFile>>windowJson;
    require(windowJson.at("version")==4 && windowJson.at("window").at("fft_size")==256 && windowJson.at("window").at("view").at("minimum_db")==-90,"Window persistence downgraded its protocol, saved draft parameters or lost its display limits");
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),windowConfig);until([&]{return state().at("state")=="ready" && state().at("configuration").at("window")==windowJson.at("window");});
    auto* spectrogram=dynamic_cast<ResearchSpectrogramView*>(widget->findChild<QWidget*>("waveform-stft"));require(spectrogram,"The actual time-frequency selection view is missing");
    const auto beforeBand=state().at("configuration").at("channel");const auto size=spectrogram->size();
    QMouseEvent bandPress(QEvent::MouseButtonPress,QPointF(100,45),QPointF(100,45),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(spectrogram,&bandPress);
    QMouseEvent bandRelease(QEvent::MouseButtonRelease,QPointF(std::max(120,size.width()-45),std::max(60,size.height()-55)),QPointF(std::max(120,size.width()-45),std::max(60,size.height()-55)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(spectrogram,&bandRelease);
    require(widget->findChild<QTabWidget*>("waveform-track-tabs")->currentIndex()==1 && inspector->findChild<QDoubleSpinBox*>("waveform-channel-passband")->value()!=200 && state().at("configuration").at("channel").at("passband_hz")==beforeBand.at("passband_hz"),"A time-frequency selection failed to prepare target parameters or silently applied an unreviewed channel");
    until([&]{const auto s=state();const auto& range=s.at("window").at("configuration").at("range");const auto& view=s.at("channel").at("configuration").at("view");return s.at("state")=="ready" && range.at("first")==view.at("first") && range.at("count")==view.at("count");});
    inspector->findChild<QCheckBox*>("waveform-channel-file-scope")->setChecked(true);inspector->findChild<QLineEdit*>("waveform-channel-file-first")->setText("0");inspector->findChild<QLineEdit*>("waveform-channel-file-count")->setText("16384");
    widget->findChild<QLineEdit*>("waveform-file-first")->setText("0");widget->findChild<QLineEdit*>("waveform-file-count")->setText("4096");widget->findChild<QPushButton*>("waveform-apply-range")->click();
    until([&]{return state().at("state")=="ready" && state().at("loaded_samples")==4096 && state().at("configuration").at("version")==5 && widget->findChild<QAction*>("waveform-save-analysis")->isEnabled();});
    require(state().at("configuration").at("channel").at("input").at("sample_count")=="16384" && state().at("channel").at("samples").get<unsigned>()>2048,"A recorded channel was restricted to its small original preview or lost its independent range");
    if(!screenshot.isEmpty()){QCoreApplication::processEvents();require(window.grab().save(screenshot+".file.png"),"Could not render independent recorded input and original preview");}
    const auto fullChannelRange=processed->range();
    QMouseEvent outsidePress(QEvent::MouseButtonPress,QPointF(processed->width()-20,35),QPointF(processed->width()-20,35),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(processed,&outsidePress);
    bool sourcePositionValid=false,filePositionValid=false;const auto sourcePosition=inspector->findChild<QLabel*>("sample.index")->text().toULongLong(&sourcePositionValid),filePosition=inspector->findChild<QLabel*>("sample.file_index")->text().toULongLong(&filePositionValid);
    require(sourcePositionValid && filePositionValid && sourcePosition>origin+4096 && filePosition==sourcePosition-origin && processed->range()==fullChannelRange,"An unloaded recorded position lost its exact source/storage mapping or clamped the processed view");
    require(inspector->findChild<QLabel*>("waveform-validity")->text().contains("原始预览"),"Unloaded original data was misclassified as a source sampling gap");
    const auto outsideB=state().at("configuration").at("view").at("cursor_b");const auto fileConfig=root/path_from_utf8("文件信道复现 📡.amc-analysis.json");chooseFile(widget->findChild<QAction*>("waveform-save-analysis"),fileConfig);
    until([&]{return state().at("last_save").value("committed",false);});std::ifstream fileConfigStream(fileConfig);Json fileJson;fileConfigStream>>fileJson;
    require(fileJson.at("version")==5 && fileJson.at("channel").at("version")==2 && fileJson.at("channel").at("input").at("identity").at("normalized_source_sha256").get<std::string>().size()==64,"The separate recorded input was saved without its declared protocol or complete identity");
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),fileConfig);until([&]{return state().at("state")=="ready" && state().at("configuration").at("view").at("cursor_b")==outsideB;});
    require(state().at("loaded_samples")==4096 && state().at("configuration").at("channel").at("input")==fileJson.at("channel").at("input"),"Reopening a file channel silently loaded or clamped an unavailable original cursor");
    {
        QTemporaryDir moved;require(moved.isValid(),"Could not create relocated analysis fixture");const auto destination=QFileInfo(moved.path()).filesystemFilePath();
        std::filesystem::copy(capture,destination/capture.filename(),std::filesystem::copy_options::recursive);std::filesystem::copy_file(fileConfig,destination/fileConfig.filename());
        require(fileJson.at("source").at("path_base")=="analysis-directory","Recorded channel configuration did not retain a portable source reference");
        std::filesystem::rename(capture/"capture.sigmf-data",capture/"temporarily-unavailable.sigmf-data");
        {
            QMainWindow freshWindow;Workspace freshWorkspace(freshWindow);LiveTools freshHost({waveformLaboratoryProvider()},"zh-CN",&freshWindow);
            for(const auto& panel:freshHost.panels()){freshWorkspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);if(panel.relativeTo)freshWorkspace.place(panel.widget,panel.relativeTo,panel.orientation);}
            freshWorkspace.restore([](bool){});freshWindow.show();chooseFile(freshHost.panels()[0].widget->findChild<QAction*>("waveform-open-analysis"),destination/fileConfig.filename());
            until([&]{return freshHost.diagnostics()[0].at("status").value("state",std::string())=="ready";});const auto fresh=freshHost.diagnostics()[0].at("status");
            require(fresh.at("loaded_samples")==4096 && fresh.at("configuration").at("channel")==fileJson.at("channel") && fresh.at("configuration").at("view").at("cursor_b")==outsideB,"A fresh laboratory could not reproduce a relocated file channel without accessing the original data path");
        }
        std::filesystem::rename(capture/"temporarily-unavailable.sigmf-data",capture/"capture.sigmf-data");
    }
    const auto savedChannel=state().at("configuration").at("channel");inspector->findChild<QDoubleSpinBox*>("waveform-channel-transition")->setValue(2000);
    widget->findChild<QPushButton*>("waveform-channel-reveal")->click();until([&]{return state().at("state")=="ready" && state().at("loaded_samples")==16384 && state().at("configuration").at("view").at("cursor_b")==outsideB && widget->findChild<QAction*>("waveform-save-analysis")->isEnabled();});
    require(state().at("configuration").at("channel")==savedChannel && widget->findChild<QTabWidget*>("waveform-track-tabs")->currentIndex()==0 && std::stoull(state().at("configuration").at("view").at("count").get<std::string>())==64,"Locating a recorded point applied draft settings, changed processed selection, or did not reveal its real original neighborhood");
    if(!screenshot.isEmpty()){QCoreApplication::processEvents();require(window.grab().save(screenshot+".original.png"),"Could not render the exact original neighborhood");}
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),windowConfig);until([&]{return state().at("state")=="ready" && state().at("configuration").at("window")==windowJson.at("window");});
    const auto completed=state().at("configuration");inspector->findChild<QDoubleSpinBox*>("waveform-channel-transition")->setValue(2000);inspector->findChild<QPushButton*>("waveform-channel-apply")->click();
    until([&]{return state().at("state")=="failed";});require(state().at("configuration")==completed,"An invalid complete channel configuration discarded the last verified result");
    {
        std::fstream data(capture/"capture.sigmf-data",std::ios::in|std::ios::out|std::ios::binary);data.seekg(stride*200);char b=0;data.read(&b,1);b^=1;data.seekp(stride*200);data.write(&b,1);
    }
    chooseFile(widget->findChild<QAction*>("waveform-open-analysis"),saved);until([&]{return state().value("state",std::string())=="failed";});
    require(state().at("configuration").at("event")==event && state().at("loaded_samples")==16384 && !host.health().isEmpty(),"Changed-source rejection lost the previous result or its diagnostic");
    QMenu missing;host.addRecordingActions(missing,{root/"missing.sigmf-meta",{},{{"modulation_id","another.failed.event"}}});missing.actions().front()->trigger();
    until([&]{return state().value("state",std::string())=="failed";});require(state().at("configuration").at("event")==event,"A failed input replaced the successful result's evidence association");
    host.setSession(true);host.setSession(false);require(state().at("loaded_samples")==16384,"Monitor lifecycle discarded a frozen recorded analysis");
}
struct FixtureLease final:ReceptionLease {std::atomic_bool released=false;void release() noexcept override{released=true;}~FixtureLease() override{release();}};
void captureWorkflow(const std::filesystem::path& root,const QString& screenshot) {
    QMainWindow window;Workspace workspace(window);LiveTools host({waveformLaboratoryProvider()},"en",&window);
    for(const auto& panel:host.panels()){workspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);if(panel.relativeTo)workspace.place(panel.widget,panel.relativeTo,panel.orientation);}
    ReceptionConsumer input;ReceptionDemand demand;std::shared_ptr<FixtureLease> lease;unsigned receiverOpened=0;
    host.setReceptionAcquire([&](const auto& requested,auto consumer){demand=requested;input=std::move(consumer);lease=std::make_shared<FixtureLease>();return lease;},[&]{++receiverOpened;});
    workspace.restore([](bool){});if(!screenshot.isEmpty())window.resize(1280,900);window.show();QCoreApplication::processEvents();
    auto* inspector=host.panels()[1].widget;auto* start=inspector->findChild<QPushButton*>("waveform-capture-start");auto* freeze=inspector->findChild<QPushButton*>("waveform-capture-freeze");
    auto* stop=inspector->findChild<QPushButton*>("waveform-capture-stop");auto* follow=inspector->findChild<QCheckBox*>("waveform-capture-follow");
    auto* tabs=inspector->findChild<QTabWidget*>();for(int n=0;n<tabs->count();++n)if(tabs->widget(n)->isAncestorOf(start))tabs->setCurrentIndex(n);
    inspector->findChild<QLineEdit*>("waveform-capture-frequency")->setText("433.920000");inspector->findChild<QDoubleSpinBox*>("waveform-capture-bandwidth")->setValue(.4);
    inspector->findChild<QPushButton*>("waveform-capture-receiver")->click();require(receiverOpened==1,"The capture panel could not navigate to generic receiver controls");
    require(start->isEnabled(),"A valid complete capture demand could not start");start->click();
    require(host.busy() && demand.frequency_hz==433920000 && demand.passband_hz==400000 && demand.minimum_rate==400000 && !demand.configured_receiver,"The research provider lost its complete input demand or depended on monitoring");
    const IqStreamParameters stream{2000000,433920000,SampleFormat::cf32_le};ReceptionStatus ready{"receiving"};ready.generation=7;ready.stream=stream;
    ready.requested={{"sample_rate_hz",2000000},{"gain_mode","auto"}};ready.applied={{"sample_rate_hz",2000000},{"filter_bandwidth_hz",600000}};ready.diagnostic={{"independent_future_field",17}};
    auto conditions=AcquisitionConditions{}.json();conditions["origin"]="provider";conditions["adc_overload"]={{"capability","available"},{"state","not_detected"},{"evidence",{{"sensor","independent.startup.sensor"}}}};conditions["future_condition"]={{"value",23}};ready.conditions=AcquisitionConditions(conditions);
    auto state=[&]{return host.diagnostics()[0].at("status");};input.changed(ready);until([&]{return state().at("capture").at("active").get<bool>();});
    require(inspector->findChild<QPlainTextEdit*>("waveform-capture-readback")->toPlainText().contains("independent_future_field"),"Capture readback discarded unknown receiver diagnostic fields");
    require(inspector->findChild<QLabel*>("waveform-capture-conditions")->text().contains("Overload during the selected window remains unknown"),"Startup ADC observation was presented as a clear selected window");
    constexpr auto origin=(1ULL<<54)+120;const auto stride=bytes_per_pair(stream.format);Bytes bytes(16384*stride);
    for(std::size_t n=0;n<16384;++n){const auto value=std::polar(.55,.21+2*std::numbers::pi*(n%16)/16.);write_iq_pair(bytes.data()+n*stride,stream.format,{float(value.real()),float(value.imag())});}
    input.offer({bytes,origin,stream,false});until([&]{return freeze->isEnabled();});freeze->click();
    until([&]{const auto s=state();return freeze->isEnabled() && s.at("state")=="ready" && s.contains("loaded_samples") && s.at("loaded_samples")==16384 && !s.at("capture").at("following").get<bool>();});
    const auto frozen=state().at("configuration");require(frozen.at("source").at("kind")=="memory-capture" && frozen.at("source").at("first_source_sample")==std::to_string(origin),"Capture source was presented as a fictitious file or lost its integer source origin");
    require(inspector->findChild<QLabel*>("sample.file_index")->text()=="Unavailable" && host.panels()[0].widget->findChild<QAction*>("waveform-save-analysis")->isEnabled(),"A memory source was shown as a fictitious file or could not be saved with its data");
    input.offer({bytes,origin+16384,stream,false});until([&]{return state().at("capture").at("processed_samples")==32768;});
    require(state().at("configuration")==frozen,"Continuing acquisition rewrote the frozen analysis");host.setSession(true);host.setSession(false);require(!lease->released && host.busy(),"Monitor lifecycle released the independent waveform receiver lease");
    if(!screenshot.isEmpty()) {
        auto* scale=host.panels()[0].widget->findChild<QComboBox*>("waveform-scale");const auto index=scale->findData(.000032);scale->setCurrentIndex(index);QMetaObject::invokeMethod(scale,"activated",Qt::DirectConnection,Q_ARG(int,index));
        QCoreApplication::processEvents();require(window.grab().save(screenshot),"Could not render the active capture workflow");
    }
    const auto captureConfig=root/path_from_utf8("捕获分析 📡.amc-analysis.json");chooseFile(host.panels()[0].widget->findChild<QAction*>("waveform-save-analysis"),captureConfig);
    until([&]{return state().at("last_save").value("committed",false);});std::ifstream savedCapture(captureConfig);Json savedConfig;savedCapture>>savedConfig;
    require(savedConfig.at("version")==2 && savedConfig.at("source").at("path_base")=="analysis-directory" && state().at("capture").at("active").get<bool>(),"Saving a portable frozen capture stopped acquisition or produced an unsupported reference");
    const auto archivedSource=captureConfig.parent_path()/path_from_utf8(savedConfig.at("source").at("metadata_path").get<std::string>());
    chooseFile(host.panels()[0].widget->findChild<QAction*>("waveform-open-recording"),archivedSource);until([&]{return state().at("state")=="ready";});
    require(state().at("loaded_samples")==16384 && inspector->findChild<QPlainTextEdit*>("waveform-provenance")->toPlainText().contains("independent_future_field"),"The generic source dialog did not load the new container or retain receiver provenance");
    const auto restoredConditions=read_signal_window(archivedSource,0,16384).processing.at(0).at("receiver_context").at("acquisition_conditions");
    require(restoredConditions==conditions,"Frozen data did not preserve startup scope, provider evidence and future conditions");
    chooseFile(host.panels()[0].widget->findChild<QAction*>("waveform-open-analysis"),captureConfig);until([&]{return state().at("state")=="ready";});
    require(state().at("configuration").at("view")==savedConfig.at("view") && inspector->findChild<QLabel*>("sample.file_index")->text()!="Unavailable","Saved capture did not reopen with its view and real stored-sample mapping");
    stop->click();until([&]{const auto s=state();return s.at("state")=="ready" && !s.at("capture").at("active").get<bool>() && s.at("loaded_samples")==32768;});
    require(lease->released && !host.busy(),"Stopping capture did not retain accepted input and release its own lease");
    const auto previous=input;inspector->findChild<QComboBox*>("waveform-capture-policy")->setCurrentIndex(1);start->click();require(demand.configured_receiver,"Manual receiver settings were not retained as a complete demand policy");
    ready.generation=8;input.changed(ready);until([&]{return state().at("capture").at("active").get<bool>();});previous.offer({bytes,origin+32768,stream,false});require(state().at("capture").at("accepted_samples")==0,"A released source callback entered a new same-parameter capture");
    const auto newOrigin=origin+1000000;input.offer({bytes,newOrigin,stream,false});until([&]{return freeze->isEnabled();});freeze->click();
    until([&]{const auto s=state();return s.at("state")=="ready" && s.at("configuration").at("source").at("first_source_sample")==std::to_string(newOrigin);});
    host.stopAll();require(lease->released && !host.busy(),"Explicit stop leaked a waveform capture lease");
    const auto beforeFailure=state().at("configuration");start->click();input.changed(ReceptionStatus{"conflict"});until([&]{return !host.health().isEmpty();});
    require(!host.busy() && state().at("configuration")==beforeFailure && inspector->findChild<QLabel*>("waveform-capture-error")->text().contains("conflict"),"Reception conflict lost the completed analysis or its actionable diagnostic");
    require(!follow->isChecked(),"A failed capture resumed automatic analysis");
}
void lateCallbacksAfterDestruction() {
    auto owner=std::make_unique<QWidget>();auto host=std::make_unique<LiveTools>(std::vector<LiveResearchProvider>{waveformLaboratoryProvider()},"en",owner.get());ReceptionConsumer late;
    host->setReceptionAcquire([&](const ReceptionDemand&,ReceptionConsumer consumer){late=std::move(consumer);return std::make_shared<FixtureLease>();});
    auto* inspector=host->panels()[1].widget;inspector->findChild<QLineEdit*>("waveform-capture-frequency")->setText("433.92");inspector->findChild<QPushButton*>("waveform-capture-start")->click();
    ReceptionStatus ready{"receiving"};ready.stream=IqStreamParameters{1000000,433920000,SampleFormat::cf32_le};ready.generation=55;late.changed(ready);
    until([&]{return host->diagnostics()[0].at("status").at("capture").at("active").get<bool>();});
    host.reset();owner.reset();bool finished=false;Bytes bytes(2048);
    std::jthread producer([consumer=std::move(late),ready,&bytes,&finished]() mutable {consumer.changed(ready);consumer.offer({bytes,700,*ready.stream,false});consumer={};finished=true;});producer.join();
    require(finished,"A late source callback retained a destroyed UI or could not release its closed worker");
}
}
int main(int argc,char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);
    app.setOrganizationName("AMC Tests");app.setApplicationName("Waveform laboratory");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try {
        if(app.arguments().contains("--review-recording")) {
            require(argc==5,"Review requires recording metadata, profile JSON and a new output directory");
            reviewRecording(QFileInfo(app.arguments()[2]).filesystemFilePath(),QFileInfo(app.arguments()[3]).filesystemFilePath(),QFileInfo(app.arguments()[4]).filesystemFilePath());return 0;
        }
        if(app.arguments().contains("--resource-check")) {
            auto tool=waveformLaboratoryProvider().create("nl-NL",nullptr);
            require(tool.widget->windowTitle()=="Independent waveform language","A resource-only language extension was not resolved");
            require(tool.widget->findChild<QAction*>("waveform-open-recording")->text()=="Open I/Q source","A missing translation did not fall back per key");
            require(tool.panels[0].widget->findChild<QPushButton*>("waveform-window-apply")->text()=="Apply to selected window","New research labels did not use the same resource-only language fallback");delete tool.widget;return 0;
        }
        QTemporaryDir directory;require(directory.isValid(),"Could not create source fixture directory");
        workflow(QFileInfo(directory.path()).filesystemFilePath(),argc>1?app.arguments()[1]:QString());
        captureWorkflow(QFileInfo(directory.path()).filesystemFilePath(),argc>2?app.arguments()[2]:QString());
        lateCallbacksAfterDestruction();
        QTemporaryDir resources;const auto target=QDir(resources.path()).filePath(QString::fromUtf8("独立语言 📡/waveform-laboratory/i18n"));
        require(QDir().mkpath(target),"Could not create plugin resource directory");
        for(const auto& file:std::filesystem::directory_iterator(resource_directory("waveform-laboratory")/"i18n"))
            std::filesystem::copy_file(file.path(),QFileInfo(QDir(target).filePath(QString::fromStdString(path_to_utf8(file.path().filename())))).filesystemFilePath());
        amc_test::write(QDir(target).filePath("nl.json"),"{\"title\":\"Independent waveform language\"}");
        amc_test::ChildRunner child(resources.path());child.setEnvironment("SIGNALSCOPE_RESOURCE_PATH",QFileInfo(target).dir().absolutePath()+"/..");
        child.run(QCoreApplication::applicationFilePath(),{"--resource-check"});
        std::cout<<"Native waveform recording entry, shared samples/cursors, applied parameter persistence, source identity, event provenance, dialog roundtrip and resource-only language extension passed.\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
