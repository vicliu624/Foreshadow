// Opt-in physical input acceptance. This executable is never registered in
// CTest and requires --run-hardware, a receiver ID and a new private output dir.
#include "plugins/desktop/research/waveform/laboratory.hpp"
#include "live_tools.hpp"
#include "workspace.hpp"
#include "signalscope/backend/sessions/reception.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/signal_capture.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <iostream>
#include <thread>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void until(F predicate,std::chrono::seconds timeout=std::chrono::seconds(30)) {
    const auto deadline=Clock::now()+timeout;
    while(Clock::now()<deadline){QCoreApplication::processEvents();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw std::runtime_error("Physical input workflow timed out");
}
void chooseFile(QAction* action,const std::filesystem::path& path) {
    require(action && action->isEnabled(),"The file action is unavailable");
    bool selected=false,expired=false;QTimer timer;timer.setInterval(20);const auto deadline=Clock::now()+std::chrono::seconds(10);
    QObject::connect(&timer,&QTimer::timeout,[&]{
        if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            if(Clock::now()>deadline){expired=true;dialog->reject();return;}
            if(auto* entry=dialog->findChild<QLineEdit*>("fileNameEdit")) {
                entry->setText(QString::fromStdString(path_to_utf8(path)));selected=true;QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
            }
        }
    });
    timer.start();action->trigger();timer.stop();require(selected && !expired,"The private file path was not selected");
}
struct Observation {
    std::atomic<std::uint64_t> samples=0,blocks=0,discontinuous_blocks=0;
    std::mutex mutex;Json states=Json::array();std::string state;
    ReceptionConsumer consumer() {
        return {[this](const ReceptionStatus& value){
            std::lock_guard lock(mutex);state=value.state;
            if(states.size()<64)states.push_back({{"state",value.state},{"detail",value.detail},{"generation",value.generation},{"requested",value.requested},{"applied",value.applied},{"diagnostic",value.diagnostic},{"conditions",value.conditions.json()}});
        },[this](const IqBlockView& block){samples+=block.bytes.size()/bytes_per_pair(block.stream.format);++blocks;if(block.discontinuous)++discontinuous_blocks;}};
    }
    Json snapshot(){std::lock_guard lock(mutex);return {{"state",state},{"samples",samples.load()},{"blocks",blocks.load()},{"discontinuous_blocks",discontinuous_blocks.load()},{"transitions",states}};}
};
// Keep observations alive in callbacks even after nonblocking lease release.
ReceptionConsumer observed(const std::shared_ptr<Observation>& observation) {
    auto consumer=observation->consumer();
    return {[observation,changed=std::move(consumer.changed)](const auto& value){changed(value);},
        [observation,offer=std::move(consumer.offer)](const auto& value){offer(value);}};
}
void acceptHardware(const std::string& receiver,const std::filesystem::path& output,unsigned seconds,Json& report) {
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QString::fromStdString(path_to_utf8(output/"settings")));
    Config base;base.frequency=433012000;base.rate=2000000;base.auto_gain=true;base.required_passband_hz=600000;
    auto session=std::make_shared<ReceiverSession>(receiver_by_name(receiver),base);
    report["description"]=session->describe();
    QMainWindow window;Workspace workspace(window);LiveTools host({waveformLaboratoryProvider()},"en",&window);
    for(const auto& panel:host.panels()){workspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);if(panel.relativeTo)workspace.place(panel.widget,panel.relativeTo,panel.orientation);}
    host.showPanel=[&](auto* panel){workspace.show(panel);};workspace.restore([](bool){});window.resize(1600,1000);window.show();
    host.setReceptionAcquire([session](const auto& demand,auto consumer){return session->acquire(demand,std::move(consumer));});
    auto state=[&]{return host.diagnostics().at(0).at("status");};
    const SignalCaptureBudget budget;
    const auto validate=[&](const Json& current){
        const auto& capture=current.at("capture");
        require(capture.at("retained_bytes").get<std::size_t>()<=budget.retention.payload_bytes,"Capture retention exceeded its payload budget");
        require(capture.at("queued_bytes").get<std::size_t>()<=budget.queue_payload_bytes && capture.at("peak_queued_bytes").get<std::size_t>()<=budget.queue_payload_bytes,"Capture queue exceeded its payload budget");
        require(capture.at("in_flight_bytes").get<std::size_t>()<=budget.maximum_block_bytes,"Capture in-flight payload exceeded its block budget");
        require(current.at("state")!="failed","Waveform analysis failed during sustained physical input");
    };
    auto monitor=std::make_shared<Observation>();ReceptionDemand demand;demand.frequency_hz=double(base.frequency);demand.passband_hz=base.required_passband_hz;demand.minimum_rate=base.rate;demand.maximum_rate=base.rate;demand.exact=base;
    auto monitorLease=session->acquire(demand,observed(monitor));host.setSession(true);
    until([&]{return monitor->samples.load()>0;});
    auto* inspector=host.panels().at(1).widget;
    auto* start=inspector->findChild<QPushButton*>("waveform-capture-start");auto* freeze=inspector->findChild<QPushButton*>("waveform-capture-freeze");auto* stop=inspector->findChild<QPushButton*>("waveform-capture-stop");auto* follow=inspector->findChild<QCheckBox*>("waveform-capture-follow");
    require(start && freeze && stop && follow,"The capture controls are missing");
    auto* tabs=inspector->findChild<QTabWidget*>();for(int i=0;i<tabs->count();++i)if(tabs->widget(i)->isAncestorOf(start))tabs->setCurrentIndex(i);
    inspector->findChild<QLineEdit*>("waveform-capture-frequency")->setText(QString::number(double(base.frequency)/1e6,'f',6));
    inspector->findChild<QDoubleSpinBox*>("waveform-capture-bandwidth")->setValue(.4);start->click();
    until([&]{return state().at("capture").at("processed_samples").get<std::uint64_t>()>0 && state().at("state")=="ready";});
    const auto started=Clock::now();report["observations"]=Json::array();report["phases"]=Json::array();
    report["budget"]={{"queue_payload_bytes",budget.queue_payload_bytes},{"maximum_block_bytes",budget.maximum_block_bytes},{"retention_payload_bytes",budget.retention.payload_bytes},{"retention_seconds",budget.retention.seconds}};
    auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
    auto record=[&](const char* phase){auto current=state();validate(current);report["phases"].push_back({{"phase",phase},{"seconds",elapsed()},{"waveform",current},{"receiver",session->snapshot()},{"independent_consumer",monitor->snapshot()}});write_json(output/"result.json",report);std::cout<<phase<<" at "<<elapsed()<<" s\n"<<std::flush;};
    double lastObservation=-1;
    auto runTo=[&](double target,bool expectCapture=true,bool allowReconfiguration=false){
        while(elapsed()<target){
            QCoreApplication::processEvents();
            if(elapsed()-lastObservation>=1){
                const auto current=state();validate(current);const auto& capture=current.at("capture");
                const bool restarting=expectCapture && allowReconfiguration &&
                    capture.at("receiver").value("state",std::string())=="starting";
                report["observations"].push_back({{"seconds",elapsed()},{"capture",capture},
                    {"analysis_state",current.at("state")},{"independent_consumer_samples",monitor->samples.load()},
                    {"planned_reconfiguration",restarting}});
                if(capture.at("active").get<bool>()!=expectCapture && !restarting){
                    report["lifecycle_failure"]={{"seconds",elapsed()},{"waveform",current},{"receiver",session->snapshot()}};
                    write_json(output/"result.json",report);
                    throw std::runtime_error("Capture activity changed outside its requested lifecycle");
                }
                lastObservation=elapsed();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(allowReconfiguration)until([&]{const auto current=state();return current.at("capture").at("active").get<bool>() &&
            current.at("capture").at("receiver").value("state",std::string())=="receiving";});
    };
    record("shared-input-following");runTo(seconds/6.0);
    until([&]{return freeze->isEnabled() && state().at("state")=="ready";});const auto beforeFreeze=state().at("configuration").at("source");
    freeze->click();until([&]{const auto current=state();return current.at("state")=="ready" && !current.at("capture").at("following").get<bool>() && current.at("configuration").at("source")!=beforeFreeze;});
    auto frozen=state().at("configuration");record("frozen-while-input-continues");runTo(seconds/3.0);
    require(state().at("configuration")==frozen,"A frozen analysis changed while capture continued");
    monitorLease->release();monitorLease.reset();host.setSession(false);
    until([&]{return session->snapshot().at("users")==1 && state().at("capture").at("active").get<bool>();});record("independent-consumer-released");runTo(seconds/2.0,true,true);
    require(state().at("configuration")==frozen,"Releasing another lease replaced the frozen analysis");
    const auto independentProgress=state().at("capture");
    until([&]{const auto current=state().at("capture");return current.at("processed_samples").get<std::uint64_t>()>independentProgress.at("processed_samples").get<std::uint64_t>() || (current.at("generation")!=independentProgress.at("generation") && current.at("processed_samples").get<std::uint64_t>()>0);});
    monitorLease=session->acquire(demand,observed(monitor));host.setSession(true);const auto monitorBefore=monitor->samples.load();until([&]{return monitor->samples.load()>monitorBefore;});record("independent-consumer-restored");
    // An incompatible new exact demand must not retune existing consumers.
    auto rejected=std::make_shared<Observation>();auto conflictDemand=demand;conflictDemand.frequency_hz+=10000000;conflictDemand.exact->frequency+=10000000;
    auto conflict=session->acquire(conflictDemand,observed(rejected));until([&]{return rejected->snapshot().at("state")=="conflict";});conflict->release();conflict.reset();report["conflict"]=rejected->snapshot();
    runTo(seconds*2.0/3,true,true);require(state().at("configuration")==frozen,"An incompatible demand replaced an accepted frozen analysis");
    until([&]{return freeze->isEnabled();});const auto beforeSecondFreeze=state().at("configuration").at("source");freeze->click();until([&]{return state().at("state")=="ready" && state().at("configuration").at("source")!=beforeSecondFreeze;});frozen=state().at("configuration");
    const auto saved=output/path_from_utf8("实时冻结 📡.amc-analysis.json");chooseFile(host.panels().at(0).widget->findChild<QAction*>("waveform-save-analysis"),saved);until([&]{return state().at("last_save").value("committed",false);});
    std::ifstream input(saved);Json configuration;input>>configuration;
    require(configuration.at("source").at("normalized_window_sha256")==frozen.at("source").at("normalized_window_sha256"),"Saved physical observations changed their normalized digest");
    chooseFile(host.panels().at(0).widget->findChild<QAction*>("waveform-open-analysis"),saved);until([&]{return state().at("state")=="ready";});
    require(state().at("configuration").at("source").at("normalized_window_sha256")==frozen.at("source").at("normalized_window_sha256") && state().at("loaded_samples").get<std::uint64_t>()==std::stoull(frozen.at("source").at("sample_count").get<std::string>()),"Reopened physical input did not reproduce its original sample identity");
    require(state().at("capture").at("active").get<bool>(),"Saving/reopening stopped live input");record("physical-freeze-save-reopen");
    require(window.grab().save(QString::fromStdString(path_to_utf8(output/"physical-freeze.png"))),"Could not render the actual captured workspace");
    runTo(seconds);record("sustained-capture-complete");report["capture_elapsed_seconds"]=elapsed();
    stop->click();until([&]{return !host.busy() && state().at("state")=="ready";});const auto afterStop=monitor->samples.load();runTo(seconds+5.0,false);
    require(monitor->samples.load()>afterStop && session->snapshot().at("users")==1,"Stopping capture stopped the independent consumer");record("capture-stopped-other-input-continues");
    monitorLease->release();monitorLease.reset();host.setSession(false);session->disconnect();report["final_receiver"]=session->snapshot();require(report.at("final_receiver").at("users")==0,"A physical receiver lease leaked");
    report["independent_consumer"]=monitor->snapshot();report["status"]="passed";write_json(output/"result.json",report);
}
}
int main(int argc,char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);app.setOrganizationName("AMC Private Hardware Acceptance");app.setApplicationName("Waveform Capture");
    std::filesystem::path output;bool outputOwned=false;Json report={{"status","not-started"},{"scope","passive physical input; actual Qt capture/freeze/save/reopen and shared lease lifecycle; independent consumer is not the detector pipeline"},{"hardware_lossless_claim",false},{"formal_installation_modified",false}};
    try {
        const auto args=app.arguments();auto argument=[&](const char* key){const auto i=args.indexOf(QString::fromLatin1(key));require(i>=0 && i+1<args.size(),"Missing required hardware acceptance argument");return args.at(i+1);};
        require(args.contains("--run-hardware"),"Explicit --run-hardware is required; this tool opens a physical input");
        const auto receiver=argument("--receiver").toUtf8().toStdString();output=path_from_utf8(argument("--output").toUtf8().toStdString());bool valid=false;const auto seconds=argument("--seconds").toUInt(&valid);require(valid && seconds>=60 && seconds<=3600,"Hardware acceptance duration must be 60..3600 seconds");
        require(output.is_absolute() && std::filesystem::create_directory(output),"Hardware acceptance requires a new absolute private output directory");
        outputOwned=true;
        report["status"]="running";report["receiver_id"]=receiver;report["requested_seconds"]=seconds;write_json(output/"result.json",report);acceptHardware(receiver,output,seconds,report);std::cout<<"Physical input acceptance passed: "<<path_to_utf8(output)<<'\n';return 0;
    }catch(const std::exception& error){report["status"]="failed";report["error"]=error.what();if(outputOwned)write_json(output/"result.json",report);std::cerr<<error.what()<<'\n';return 1;}
}
