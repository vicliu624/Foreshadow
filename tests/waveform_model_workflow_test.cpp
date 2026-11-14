#include "plugins/desktop/research/waveform/laboratory.hpp"
#include "plugins/desktop/research/waveform/model_panel.hpp"
#include "live_tools.hpp"
#include "workspace.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "test_process.hpp"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <fstream>
#include <numbers>
#include <source_location>
#include <thread>

using namespace signalscope;
using namespace signalscope::desktop;
using amc_test::require;
namespace {
template<class F>void until(F predicate,std::source_location location=std::source_location::current()) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<end){QCoreApplication::processEvents();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error("Model workflow timeout at line "+std::to_string(location.line()));
}
void choose(QAction* action,const std::filesystem::path& path) {
    require(action && action->isEnabled(),"Expected an available file action");QTimer timer;bool selected=false;timer.setInterval(20);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    QObject::connect(&timer,&QTimer::timeout,[&]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        if(std::chrono::steady_clock::now()>end){dialog->reject();return;}auto* field=dialog->findChild<QLineEdit*>("fileNameEdit");require(field,"Missing actual file dialog entry");field->setText(QString::fromStdString(path_to_utf8(path)));selected=true;QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
    }});timer.start();action->trigger();timer.stop();require(selected,"File dialog did not select a path");
}
struct Fixture {
    QMainWindow window;Workspace workspace{window};LiveTools host{{waveformLaboratoryProvider()},"zh-CN",&window};
    Fixture(){for(const auto& panel:host.panels()){workspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);if(panel.relativeTo)workspace.place(panel.widget,panel.relativeTo,panel.orientation);}host.showPanel=[this](auto* panel){workspace.show(panel);};workspace.restore([](bool){});window.setWindowState(Qt::WindowNoState);window.resize(QCoreApplication::arguments().contains("--compact")?QSize(640,480):QSize(1600,1000));window.show();QCoreApplication::processEvents();}
    QWidget* main(){return host.panels()[0].widget;}QWidget* inspector(){return host.panels()[1].widget;}
    Json state(){return host.diagnostics()[0].at("status");}
    template<class T>T* control(const char* name){auto* value=inspector()->findChild<T*>(name);require(value,"Missing model workflow control");return value;}
    void number(const char* key,double value){control<QDoubleSpinBox>((std::string("waveform-model-param-")+key).c_str())->setValue(value);}
    void waitReady(){until([&]{const auto value=state();if(value.at("state")=="failed")throw std::runtime_error("Model workflow failed: "+host.health().toStdString());return value.at("state")=="ready";});}
    void waitSave(){until([&]{return state().at("state")=="ready" && main()->findChild<QAction*>("waveform-save-analysis")->isEnabled();});}
};
void click(TraceView* view,double fraction) {
    const QPointF point(view->width()*fraction,view->height()*.5);QMouseEvent event(QEvent::MouseButtonPress,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&event);QCoreApplication::processEvents();
}
void workflow(const std::filesystem::path& root,const QString& screenshot) {
    constexpr std::uint64_t origin=(1ULL<<54)+29;constexpr unsigned rate=8000,count=8192;const auto capture=root/path_from_utf8("模型真值 📡");Config config;config.rate=rate;config.frequency=433012000;
    const auto format=SampleFormat::cf32_le;const auto stride=bytes_per_pair(format);Bytes bytes(count*stride);const auto gain=std::polar(.63,.23);const std::complex<double> dc{.04,-.03};
    // Independent continuous-time truth; do not call a reference provider.
    for(unsigned n=0;n<count;++n){const auto t=double(n)/rate;const auto value=gain*std::exp(std::complex<double>(0,2*std::numbers::pi*(-50*t+156.25*t*t)))+dc;write_iq_pair(bytes.data()+n*stride,format,{float(value.real()),float(value.imag())});}
    Recorder recorder(capture,config,"independent-model-truth",format);recorder.append(bytes);recorder.finish("complete",{{"stream_first_sample",origin}});const auto metadata=capture/"capture.sigmf-meta";
    Fixture ui;QMenu menu;ui.host.addRecordingActions(menu,{metadata,ResearchSampleRange{0,4096},{{"future_model_evidence",{{"opaque",19}}}}});require(menu.actions().size()==1,"No recorded model entry");menu.actions().front()->trigger();ui.waitReady();
    auto* raw=dynamic_cast<TraceView*>(ui.main()->findChild<QWidget*>("waveform-track-0"));require(raw,"Missing raw view");raw->setRange({origin+64,512});
    ui.control<QCheckBox>("waveform-model-enabled")->setChecked(true);auto* provider=ui.control<QComboBox>("waveform-model-provider");provider->setCurrentIndex(provider->findData("linear-chirp"));
    ui.number("amplitude",1);ui.number("phase_rad",0);ui.number("carrier_hz",70);ui.number("start_hz",-120);ui.number("end_hz",200);ui.number("duration_seconds",1.024);
    ui.control<QCheckBox>("waveform-model-fit-dc")->setChecked(true);ui.control<QDoubleSpinBox>("waveform-model-time-offset")->setValue(8);ui.control<QSpinBox>("waveform-model-delay-min")->setValue(-3);ui.control<QSpinBox>("waveform-model-delay-max")->setValue(3);
    ui.control<QPushButton>("waveform-model-apply")->click();ui.waitReady();auto state=ui.state();
    require(state.at("configuration").at("version")==6 && state.at("model").at("observations")==512 && state.at("model").at("residual_rms").get<double>()<5e-8,"Raw model comparison did not recover independent truth");
    require(state.at("model").at("method").at("delay_samples")==0 && state.at("model").at("method").at("tied_delay_samples").size()==1,"Chirp alignment did not preserve its known reference time");
    const auto method=state.at("model").at("method"),applied=state.at("configuration").at("model");
    auto* model=dynamic_cast<TraceView*>(ui.main()->findChild<QWidget*>("waveform-model-track-0"));require(model,"Missing actual model track");model->zoom(2);click(model,.7);
    require(ui.state().at("model").at("method")==method && ui.state().at("configuration").at("model").at("range")==applied.at("range"),"Zooming or picking a cursor silently refitted the reference origin");
    model->grab();require(model->renderedPrimitives()>0,"The observed/reference overlay did not draw");
    ui.control<QCheckBox>("waveform-window-enabled")->setChecked(true);auto* fft=ui.control<QComboBox>("waveform-window-fft");fft->setCurrentIndex(fft->findData(64));ui.control<QSpinBox>("waveform-window-hop")->setValue(32);ui.control<QPushButton>("waveform-window-apply")->click();ui.waitReady();
    require(ui.state().at("model").at("method")==method && ui.state().at("window").contains("observed_samples"),"Window research replaced an applied model");
    ui.number("carrier_hz",900);ui.waitSave();const auto saved=root/path_from_utf8("原始模型 📡.amc-analysis.json");choose(ui.main()->findChild<QAction*>("waveform-save-analysis"),saved);until([&]{return ui.state().at("last_save").value("committed",false);});
    std::ifstream input(saved);Json savedJson;input>>savedJson;require(savedJson.at("version")==6 && savedJson.at("model").at("parameters").at("carrier_hz")==70 && savedJson.at("model").at("range")==applied.at("range"),"Model saving recorded draft values or changed the applied range");
    const auto completed=ui.state().at("configuration");
    const auto reject=[&](Json bad,const char* name){const auto path=root/name;write_json(path,bad);choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),path);until([&]{return ui.state().at("state")=="failed";});require(ui.state().at("configuration")==completed,"Rejected model restore changed the previously completed analysis");};
    auto bad=savedJson;bad["model"]["method_version"]=99;reject(bad,"bad-method.json");bad=savedJson;bad["model"]["input_clock"]["rate_hz"]=8001;reject(bad,"bad-clock.json");bad=savedJson;bad["model"]["provider_id"]="independent.missing";reject(bad,"bad-provider.json");bad=savedJson;bad["version"]=5;reject(bad,"bad-protocol.json");
    choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),saved);ui.waitReady();require(ui.state().at("configuration").at("model")==savedJson.at("model") && ui.state().at("model").at("method")==method,"Version 6 model view or method did not reproduce");
    auto legacyMethod=savedJson;legacyMethod["model"]["method"]["version"]=2;legacyMethod["model"]["method"].erase("delay_time_encoding");const auto oldMethodPath=root/"version-6-legacy-comparison.json";write_json(oldMethodPath,legacyMethod);choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),oldMethodPath);ui.waitReady();require(ui.state().at("configuration").at("model").at("parameters")==savedJson.at("model").at("parameters") && ui.state().at("model").at("residual_rms").get<double>()<5e-8 && ui.state().at("model").at("method").at("version")==3,"A pre-v3 method record prevented reproduction of an existing version 6 analysis");
    const auto beforeCancel=ui.state().at("configuration");ui.control<QSpinBox>("waveform-model-delay-min")->setValue(-128);ui.control<QSpinBox>("waveform-model-delay-max")->setValue(128);ui.control<QPushButton>("waveform-model-apply")->click();ui.main()->findChild<QAction*>("waveform-cancel-analysis")->trigger();require(ui.state().at("configuration")==beforeCancel,"Canceling model work discarded the completed result");
    ui.control<QSpinBox>("waveform-model-delay-min")->setValue(-3);ui.control<QSpinBox>("waveform-model-delay-max")->setValue(3);
    // Failed provider-owned whole-configuration validation retains the result.
    const auto retained=ui.state().at("configuration");ui.number("carrier_hz",3900);ui.number("end_hz",3900);ui.control<QPushButton>("waveform-model-apply")->click();until([&]{return ui.state().at("state")=="failed";});require(ui.state().at("configuration")==retained && ui.control<QLabel>("waveform-model-issues")->text().contains(QString::fromUtf8("奈奎斯特")),"Model validation lost the completed result or its own translated explanation");
    choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),saved);ui.waitReady();
    ui.control<QCheckBox>("waveform-model-enabled")->setChecked(false);ui.control<QPushButton>("waveform-model-apply")->click();require(!ui.state().at("model").contains("observations") && ui.state().at("window").contains("observed_samples"),"Disabling a model removed independent window research");
    ui.control<QCheckBox>("waveform-window-enabled")->setChecked(false);ui.control<QPushButton>("waveform-window-apply")->click();
    ui.control<QCheckBox>("waveform-channel-enabled")->setChecked(true);ui.control<QDoubleSpinBox>("waveform-channel-passband")->setValue(1);ui.control<QDoubleSpinBox>("waveform-channel-transition")->setValue(.5);ui.control<QSpinBox>("waveform-channel-rate")->setValue(2000);
    ui.control<QCheckBox>("waveform-channel-file-scope")->setChecked(true);ui.control<QLineEdit>("waveform-channel-file-first")->setText("0");ui.control<QLineEdit>("waveform-channel-file-count")->setText(QString::number(count));ui.control<QPushButton>("waveform-channel-apply")->click();ui.waitReady();
    auto* channel=dynamic_cast<TraceView*>(ui.main()->findChild<QWidget*>("waveform-channel-track-0"));require(channel,"Missing processed model input");channel->setRange({1200,256});
    ui.control<QCheckBox>("waveform-model-enabled")->setChecked(true);ui.control<QComboBox>("waveform-model-input")->setCurrentIndex(1);ui.control<QDoubleSpinBox>("waveform-model-time-offset")->setValue(600);ui.control<QPushButton>("waveform-model-apply")->click();ui.waitReady();state=ui.state();
    require(state.at("loaded_samples")==4096 && state.at("model").at("observations")==256 && state.at("model").at("residual_rms").get<double>()<.001,"Processed comparison did not use the independent file channel clock and observations");
    require(state.at("configuration").at("model").at("input_clock").at("source_origin")==std::to_string(origin) && state.at("configuration").at("model").at("input_clock").at("rate_hz")==2000,"Processed model persistence used a raw or rounded clock");
    ui.main()->findChild<QTabWidget*>("waveform-track-tabs")->setCurrentIndex(3);ui.inspector()->findChild<QTabWidget*>("waveform-inspector-tabs")->setCurrentIndex(4);model->grab();
    click(model,.8);if(QCoreApplication::arguments().contains("--compact")){require(ui.window.width()==640 && ui.window.height()==480,"The data-filled model workspace exceeded the compact window size");require(model->visibleRegion().boundingRect().height()>=90,"Compact model explanatory text displaced the usable plot");}if(!screenshot.isEmpty()){QCoreApplication::processEvents();require(ui.window.grab().save(screenshot),"Could not render the actual model workflow");}
    ui.waitSave();const auto processed=root/path_from_utf8("处理模型 📡.amc-analysis.json");choose(ui.main()->findChild<QAction*>("waveform-save-analysis"),processed);until([&]{return ui.state().at("last_save").value("committed",false);});std::ifstream processedFile(processed);Json processedJson;processedFile>>processedJson;
    require(processedJson.at("source").at("path_base")=="analysis-directory","Model configuration did not keep a portable source reference");
    const auto beforeReveal=ui.state().at("configuration").at("model");ui.main()->findChild<QPushButton*>("waveform-channel-reveal")->click();ui.waitReady();
    require(ui.state().at("loaded_samples")==8192 && ui.state().at("configuration").at("model")==beforeReveal,"Locating an unloaded original point lost or realigned its processed model");
    // Move the source and analysis, then open in a new component instance.
    const auto moved=root/path_from_utf8("整体移动 📡");std::filesystem::create_directory(moved);std::filesystem::copy(capture,moved/capture.filename(),std::filesystem::copy_options::recursive);std::filesystem::copy_file(processed,moved/processed.filename());std::filesystem::rename(capture/"capture.sigmf-data",capture/"unavailable.data");
    {Fixture fresh;choose(fresh.main()->findChild<QAction*>("waveform-open-analysis"),moved/processed.filename());fresh.waitReady();require(fresh.state().at("loaded_samples")==4096 && fresh.state().at("configuration").at("model")==processedJson.at("model") && fresh.state().at("model").at("residual_rms").get<double>()<.001,"A fresh moved analysis did not reproduce the processed model and view");}
    std::filesystem::rename(capture/"unavailable.data",capture/"capture.sigmf-data");
    // A successful replacement of the raw/processed input invalidates its model.
    ui.control<QPushButton>("waveform-channel-apply")->click();ui.waitReady();require(!ui.state().at("configuration").at("model").at("enabled").get<bool>(),"A changed input silently retained a model from an earlier result");
}
void digitalModels(const std::filesystem::path& root,const QString& screenshot) {
    constexpr unsigned rate=8000;const auto format=SampleFormat::cf32_le;const auto stride=bytes_per_pair(format);const auto gain=std::polar(.61,.13);Config config;config.rate=rate;config.frequency=433012000;
    const auto record=[&](const std::filesystem::path& path,unsigned count,auto value){Bytes bytes(count*stride);for(unsigned n=0;n<count;++n){const auto sample=gain*value(n);write_iq_pair(bytes.data()+n*stride,format,{float(sample.real()),float(sample.imag())});}Recorder recorder(path,config,"independent-digital-ui-truth",format);recorder.append(bytes);recorder.finish("complete",{{"stream_first_sample",(1ULL<<54)+789}});return path/"capture.sigmf-meta";};
    const std::array<unsigned,4> indices{0,1,3,2};const auto psk=record(root/"known-psk",1024,[&](unsigned n){return std::polar(1.,2*std::numbers::pi*indices[((n+3)/4)%4]/4.);});
    Fixture ui;choose(ui.main()->findChild<QAction*>("waveform-open-recording"),psk);ui.waitReady();auto* choices=ui.control<QComboBox>("waveform-model-provider");choices->setCurrentIndex(choices->findData("known-symbol-psk"));ui.control<QCheckBox>("waveform-model-enabled")->setChecked(true);ui.number("symbol_rate_hz",2000);ui.control<QLineEdit>("waveform-model-param-symbols")->setText("0 1 3 2");ui.control<QSpinBox>("waveform-model-delay-min")->setValue(-5);ui.control<QSpinBox>("waveform-model-delay-max")->setValue(5);ui.control<QPushButton>("waveform-model-apply")->click();ui.waitReady();
    require(ui.state().at("model").at("residual_rms").get<double>()<5e-8 && ui.state().at("model").at("method").at("delay_samples")==3 && ui.state().at("model").at("method").at("provider_protocol_version")==3,"PSK did not enter the existing generic UI with exact integer alignment");
    const auto path=root/path_from_utf8("PSK 对照 📡.amc-analysis.json");choose(ui.main()->findChild<QAction*>("waveform-save-analysis"),path);until([&]{return ui.state().at("last_save").value("committed",false);});const auto applied=ui.state().at("configuration").at("model");choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),path);ui.waitReady();require(ui.state().at("configuration").at("model")==applied,"The known PSK method, symbols and alignment did not roundtrip");
    if(!screenshot.isEmpty())require(ui.window.grab().save(screenshot+".psk.png"),"Could not render PSK model controls");
    auto* pulse=ui.control<QComboBox>("waveform-model-param-pulse_shape");pulse->setCurrentIndex(pulse->findData("\"rrc\""));ui.control<QPushButton>("waveform-model-apply")->click();ui.waitReady();require(ui.state().at("model").at("residual_rms").get<double>()>.03 && ui.state().at("model").at("method").at("provider_method").at("pulse")=="symmetric-truncated-root-raised-cosine" && ui.state().at("model").at("method").at("evm")=="not-established","Wrong pulse shaping appeared to explain a rectangular PSK observation or established EVM");
    const auto ofdm=record(root/"known-ofdm",800,[](unsigned n){const auto shifted=n+2,symbol=(shifted/40)%2;const auto local=static_cast<int>(shifted%40)-8;const auto wave=[&](int bin){return std::polar(1.,2*std::numbers::pi*bin*local/32.);};return (symbol==0?wave(-3)+std::complex<double>(0,.4)*wave(2):std::complex<double>(0,-1)*wave(1)+.5*wave(4))/std::sqrt(32.);});
    choose(ui.main()->findChild<QAction*>("waveform-open-recording"),ofdm);ui.waitReady();require(!ui.state().at("model").contains("observations"),"A new input retained the PSK comparison");choices->setCurrentIndex(choices->findData("known-subcarrier-ofdm"));ui.number("fft_size",32);ui.number("prefix_samples",8);ui.control<QLineEdit>("waveform-model-param-subcarriers")->setText("-3:1,0 2:0,0.4 | 1:0,-1 4:0.5,0");ui.control<QPushButton>("waveform-model-apply")->click();ui.waitReady();
    require(ui.state().at("model").at("residual_rms").get<double>()<5e-8 && ui.state().at("model").at("method").at("delay_samples")==2 && ui.state().at("model").at("method").at("evm")=="not-established","OFDM did not use known subcarriers/prefix or falsely established EVM");
    const auto saved=root/path_from_utf8("OFDM 对照 📡.amc-analysis.json");choose(ui.main()->findChild<QAction*>("waveform-save-analysis"),saved);until([&]{return ui.state().at("last_save").value("committed",false);});const auto completed=ui.state().at("configuration").at("model");
    ui.number("prefix_samples",16);ui.number("fft_size",8);require(ui.control<QDoubleSpinBox>("waveform-model-param-prefix_samples")->value()==16,"OFDM whole-configuration bounds silently rewrote a draft prefix");ui.control<QPushButton>("waveform-model-apply")->click();until([&]{return ui.state().at("state")=="failed";});require(ui.state().at("configuration").at("model")==completed,"An invalid prefix discarded the completed OFDM model");
    choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),saved);ui.waitReady();if(!screenshot.isEmpty())require(ui.window.grab().save(screenshot+".ofdm.png"),"Could not render OFDM model controls");
    Fixture fresh;choose(fresh.main()->findChild<QAction*>("waveform-open-analysis"),saved);fresh.waitReady();require(fresh.state().at("configuration").at("model")==completed && fresh.state().at("model").at("residual_rms").get<double>()<5e-8,"OFDM did not restore its explicit two-symbol reference in a fresh component");
}
struct Lease final:ReceptionLease {bool released=false;void release() noexcept override{released=true;}};
void captureModel(const std::filesystem::path& root) {
    Fixture ui;ReceptionConsumer input;auto lease=std::make_shared<Lease>();ui.host.setReceptionAcquire([&](const ReceptionDemand&,ReceptionConsumer consumer){input=std::move(consumer);return lease;});
    ui.control<QLineEdit>("waveform-capture-frequency")->setText("433.012000");ui.control<QDoubleSpinBox>("waveform-capture-bandwidth")->setValue(.002);ui.control<QPushButton>("waveform-capture-start")->click();
    const IqStreamParameters stream{8000,433012000,SampleFormat::cf32_le};ReceptionStatus ready{"receiving"};ready.generation=91;ready.stream=stream;input.changed(ready);until([&]{return ui.state().at("capture").at("active").get<bool>();});
    constexpr std::uint64_t origin=(1ULL<<54)+2345;const auto stride=bytes_per_pair(stream.format);Bytes bytes(1024*stride);for(unsigned n=0;n<1024;++n){const auto phase=.17+2*std::numbers::pi*n/16.;write_iq_pair(bytes.data()+n*stride,stream.format,{float(.6*std::cos(phase)),float(.6*std::sin(phase))});}
    input.offer({bytes,origin,stream,false});auto* freeze=ui.control<QPushButton>("waveform-capture-freeze");until([&]{return freeze->isEnabled();});freeze->click();until([&]{const auto value=ui.state();return freeze->isEnabled() && value.at("state")=="ready" && value.contains("loaded_samples") && value.at("loaded_samples")==1024 && !value.at("capture").at("following").get<bool>();});
    ui.control<QCheckBox>("waveform-model-enabled")->setChecked(true);auto* provider=ui.control<QComboBox>("waveform-model-provider");provider->setCurrentIndex(provider->findData("constant-frequency"));ui.number("carrier_hz",500);ui.control<QPushButton>("waveform-model-apply")->click();ui.waitReady();
    require(ui.state().at("configuration").at("source").at("kind")=="memory-capture" && ui.state().at("model").at("observations")==1024 && ui.state().at("model").at("residual_rms").get<double>()<5e-8,"A frozen in-memory source could not use the same model workflow");
    const auto method=ui.state().at("model").at("method");const auto path=root/path_from_utf8("冻结模型 📡.amc-analysis.json");choose(ui.main()->findChild<QAction*>("waveform-save-analysis"),path);until([&]{return ui.state().at("last_save").value("committed",false);});std::ifstream file(path);Json saved;file>>saved;
    require(saved.at("version")==6 && saved.at("source").at("path_base")=="analysis-directory" && ui.state().at("capture").at("active").get<bool>(),"Archiving a model downgraded the configuration or stopped capture");ui.host.stopAll();require(lease->released,"The model workflow leaked its independent capture lease");
    choose(ui.main()->findChild<QAction*>("waveform-open-analysis"),path);ui.waitReady();require(ui.state().at("model").at("method")==method && ui.state().at("configuration").at("model")==saved.at("model"),"An archived frozen model did not reproduce its source clock, method and view");
}
void independentResources(const std::filesystem::path& root) {
    ReferenceModelProvider provider;provider.id="independent.triangle";provider.resource_namespace="independent-reference";provider.label_key="label";provider.description_key="about";provider.protocol_version=2;
    provider.describe=[](const auto&,const Json&){return Json::array({{{"key","level"},{"type","number"},{"extension",true},{"label_key","level"},{"minimum",.1},{"maximum",2},{"default",1}}});};
    provider.prepare=[](const auto&,const Json& values){PreparedReferenceModel result;result.parameters=values;const double level=values.value("level",1.);result.workspace_payload_bytes=sizeof(double);result.evaluate=[level](const ReferenceModelPoint& point){const auto t=point.seconds();const auto fraction=t-std::floor(t);return std::complex<double>(level*(1-4*std::abs(fraction-.5L)),.25*level);};result.method={{"future_evidence",{{"opaque",73}}}};return result;};
    const auto resources=root/"resources";std::filesystem::create_directory(resources);std::filesystem::copy(resource_directory("waveform-laboratory"),resources/"waveform-laboratory",std::filesystem::copy_options::recursive);
    for(const auto& model:reference_model_providers())if(!std::filesystem::exists(resources/model.resource_namespace))std::filesystem::copy(resource_directory(model.resource_namespace),resources/model.resource_namespace,std::filesystem::copy_options::recursive);register_reference_model(provider);
    const auto own=resources/"independent-reference"/"i18n";std::filesystem::create_directories(own);write_json(own/"en.json",{{"label","Independent triangle"},{"about","Independent reference description"},{"level","Reference level"}});write_json(own/"fr.json",{{"label","Triangle indépendant"}});
    qputenv("SIGNALSCOPE_RESOURCE_PATH",QByteArray::fromStdString(path_to_utf8(resources)));WaveformModelPanel panel("fr-FR");auto* choices=panel.controls()->findChild<QComboBox*>("waveform-model-provider");const auto selected=choices->findData(QString::fromStdString(provider.id));require(selected>=0 && choices->itemText(selected)==QString::fromUtf8("Triangle indépendant"),"An independent model and resource-only language did not enter the selector");choices->setCurrentIndex(selected);
    require(panel.controls()->findChild<QLabel*>("waveform-model-about")->text()=="Independent reference description","A missing provider translation did not fall back per key");
    auto source=std::make_shared<SignalWindow>();source->stream={100,433000000,SampleFormat::cf32_le};SignalSegment segment;segment.first_sample=200;segment.samples.resize(100);for(unsigned n=0;n<100;++n){const auto t=n/100.;segment.samples[n]={float(1-4*std::abs(t-.5)),.25f};}source->segments.push_back(std::move(segment));auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));panel.setInputs(analysis,{});panel.setSelection({200,100},{});panel.controls()->findChild<QCheckBox*>("waveform-model-enabled")->setChecked(true);const auto request=panel.draft();require(request && request->provider_id==provider.id && request->parameters.at("level")==1,"An independent provider needed a parameter consumer branch");
    auto result=std::make_shared<ReferenceComparison>(compare_reference_model(analysis,*request->range,provider,request->parameters,request->fit));auto applied=*request;applied.expected_clock=SignalSampleClock{0,100,100};panel.setResult(result,applied);panel.resize(1000,700);panel.show();QCoreApplication::processEvents();auto* view=dynamic_cast<TraceView*>(panel.findChild<QWidget*>("waveform-model-track-0"));view->grab();require(result->residual_rms<1e-7 && view->renderedPrimitives()>0 && panel.snapshot().at("method").at("provider_method").at("future_evidence").at("opaque")==73,"The independent model did not enter comparison, rendering and opaque method preservation");
    const auto saved=panel.configuration();require(WaveformModelPanel::readConfiguration(saved)->provider_id==provider.id && saved.at("method").at("provider_method").at("future_evidence").at("opaque")==73,"Independent model configuration required a central model branch or lost unknown method evidence");
}
}
int main(int argc,char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);app.setOrganizationName("AMC Tests");app.setApplicationName("Model workflow");QTemporaryDir directory;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    try {require(directory.isValid(),"No temporary model fixture directory");const auto root=QFileInfo(directory.path()).filesystemFilePath();const auto screenshot=argc>1?app.arguments()[1]:QString();workflow(root,screenshot);digitalModels(root,screenshot);captureModel(root);independentResources(root);std::cout<<"Model raw/processed/frozen workflow, known PSK/OFDM, independent truth, immutable application, version 6 roundtrip, failure retention, relocated source and independent provider/resources passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
