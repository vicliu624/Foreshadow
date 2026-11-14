#include "live_fixture_provider.hpp"
#include "live_tools.hpp"
#include "workspace.hpp"
#include "plugins/desktop/research/builtin/fm_live.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "test_process.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>
#include <thread>
using namespace signalscope;
using namespace signalscope::desktop;
using amc_test::require;
namespace {
struct FixtureLease final:ReceptionLease {bool released=false;void release() noexcept override{released=true;}~FixtureLease() override{release();}};
void independentLifecycle(const std::filesystem::path& resources) {
    QWidget owner;auto state=std::make_shared<LiveFixtureState>();LiveTools host({independentReceptionProvider(resources,state)},"fr-CA",&owner);
    ReceptionConsumer input;ReceptionDemand requested;auto lease=std::make_shared<FixtureLease>();
    host.setReceptionAcquire([&](const auto& demand,auto consumer){requested=demand;input=std::move(consumer);return lease;});
    QMenu menu;host.addEventActions(menu,Json::object());require(menu.actions().size()==1,"Independent v2 request action was hidden");menu.actions().front()->trigger();
    require(host.busy() && requested.frequency_hz==433920000 && requested.passband_hz==400000,"Independent provider could not request input before monitoring");
    const auto stopped=state->stopped;host.setSession(false);require(host.busy() && state->stopped==stopped,"Stopping monitor released a v2 independent lease");
    const IqStreamParameters stream{1000000,433920000,SampleFormat::ci16_le};Bytes bytes(400);
    std::jthread producer([&]{ReceptionStatus ready{"receiving"};ready.stream=stream;input.changed(ready);input.offer({bytes,17000,stream,false});});producer.join();
    require(state->pairs==0,"I/Q reached a decoder before the UI applied the stream configuration");QCoreApplication::processEvents();input.offer({bytes,17100,stream,false});
    require(host.diagnostics().front().at("configuration_wait_pairs_total")==100,"Input ignored while applying configuration was hidden from stage diagnostics");
    require(state->context.receiving && state->pairs==100,"Independent input callbacks were lost or not marshalled to the UI");
    host.setSession(true);host.subscriber()({bytes,0,stream,false});QCoreApplication::processEvents();require(state->pairs==100,"Independent v2 tool was also fed by monitor");
    ReceptionStatus starting{"starting"},reconfigured{"receiving"};reconfigured.stream=IqStreamParameters{500000,434000000,SampleFormat::ci16_le};input.changed(starting);input.changed(reconfigured);input.offer({bytes,0,*reconfigured.stream,false});require(state->pairs==100,"A new stream was delivered to the previous decoder generation");QCoreApplication::processEvents();input.offer({bytes,100,*reconfigured.stream,false});require(state->context.stream->sample_rate==500000 && state->pairs==200,"The newest receiver configuration did not resume independent input");
    host.stopAll();require(lease->released && !host.busy(),"Independent release did not reach the input lease");
    ReceptionStatus late{"receiving"};late.stream=stream;input.changed(late);input.offer({bytes,17100,stream,false});QCoreApplication::processEvents();
    require(!state->context.receiving && state->pairs==200,"A late input callback revived a released consumer");
    lease->released=false;state->reject=true;menu.actions().front()->trigger();input.changed(late);QCoreApplication::processEvents();
    require(lease->released && !host.busy() && state->failures==1 && host.health().contains("failed"),"A status callback exception escaped the host, leaked input or lost its persistent diagnostic");
}
void providerExtension(const std::filesystem::path& resources) {
    auto healthy=std::make_shared<LiveFixtureState>(),broken=std::make_shared<LiveFixtureState>();broken->reject=true;
    auto added=liveFixtureProvider(resources,"independent.counter",healthy);added.protocolVersion=1;registerLiveResearchProvider(added);
    const auto registered=liveResearchProviders();
    const auto found=std::find_if(registered.begin(),registered.end(),[&](const auto& p){return p.id==added.id;});
    require(found!=registered.end(),"Independent live provider was not registered");
    QMainWindow window;Workspace workspace(window);
    LiveTools host({*found,liveFixtureProvider(resources,"independent.failure",broken)},"fr-CA",&window);
    for(const auto& p:host.panels()){workspace.add(p.id,p.title,p.widget,Qt::RightDockWidgetArea);workspace.setVisible(p.widget,p.initiallyVisible);}
    host.showPanel=[&](QWidget* widget){workspace.show(widget);};workspace.restore([](bool){});window.show();QCoreApplication::processEvents();
    require(host.panels().front().title==QString::fromUtf8("Compteur indépendant"),"Provider-owned language fallback did not resolve");
    const auto viewName="view/"+host.panels().front().id;
    auto* toggle=window.findChild<QAction*>(viewName);require(toggle,"Independent live panel did not enter View automatically");
    host.setSession(true);auto subscriber=host.subscriber();Bytes data{1,2,3,4,5,6,7,8};
    const IqStreamParameters source{2000000,433920000,SampleFormat::ci16_le};
    std::jthread producer([&]{for(unsigned i=0;i<1000;++i)subscriber({data,9000+2ULL*i,source,false});});producer.join();QCoreApplication::processEvents();
    require(healthy->pairs==2000 && healthy->first==10998,"Live host lost arbitrary-origin I/Q or changed its timeline");
    require(host.diagnostics().front().at("status").at("independent_pairs")==2000,"Generic diagnostics did not preserve the independent provider payload");
    require(healthy->context.receiving && healthy->context.stream && healthy->context.stream->center_frequency_hz==433920000,"Live source context was not published on the UI thread");
    require(broken->failures==1 && broken->pairs==0,"A failed optional consumer flooded failures or interrupted another provider");
    data.assign(8,0);require(healthy->retained==Bytes({1,2,3,4,5,6,7,8}),"Provider did not preserve the borrowed sample lifetime contract");
    QMenu actions;host.addEventActions(actions,{{"modulation_id","independent.future.signal"},{"features",{{"new_feature",17}}}});
    require(actions.actions().size()==2 && actions.actions().front()->text()=="Inspect independent signal","Unknown module event actions were hidden or lacked per-key English fallback");
    actions.actions().front()->trigger();QCoreApplication::processEvents();require(workspace.visible(host.panels().front().widget) && toggle->isChecked() && host.busy(),"Live action did not open its independently registered panel");
    toggle->trigger();require(host.busy(),"Hiding a panel incorrectly stopped its module");host.stopAll();require(!host.busy(),"Generic stop did not stop the independent tool");
    host.setSession(true);subscriber({data,30000,source,false});host.setSession(false);QCoreApplication::processEvents();
    require(!healthy->context.receiving && !healthy->context.stream,"A stale queued source callback revived a stopped session");
    const auto count=healthy->pairs.load();subscriber({data,30002,source,false});require(healthy->pairs==count,"Stopped live tools still received capture I/Q");
    broken->reject=false;host.setSession(true);subscriber({data,40000,source,false});QCoreApplication::processEvents();require(broken->pairs==2,"A new session did not recover a faulted consumer");host.setSession(false);
}
void fmControls(const QString& screenshot={}) {
    auto provider=fmLiveResearchProvider();QWidget owner;auto tool=provider.create("en",&owner);tool.widget->resize(410,780);tool.widget->show();
    auto* start=tool.widget->findChild<QPushButton*>("fm-live-start");auto* frequency=tool.widget->findChild<QLineEdit*>("fm-live-frequency");
    auto* reason=tool.widget->findChild<QLabel*>("fm-live-validation");require(start && frequency && reason && !start->isEnabled(),"FM could start without a source or lacked visible validation");
    tool.sourceChanged({true,IqStreamParameters{10000000,100000000,SampleFormat::ci16_le}});
    frequency->setText("105");require(!start->isEnabled() && reason->text().contains("95.110") && reason->text().contains("104.890"),"Out-of-range FM configuration was not blocked as a complete channel");
    frequency->setText("97");require(!reason->text().contains("104.890"),"A valid manual frequency retained the previous range conflict");
    auto actions=tool.actions({{"evidence_level","broadcast_fm_stereo"},{"center_frequency_hz",98000000}});require(actions.size()==3,"FM module did not own its event and favorite actions");
    actions.front().trigger();require(frequency->text().startsWith("98"),"FM event did not select its frequency in the module panel");
    auto* volume=tool.widget->findChild<QSlider*>("fm-live-volume");auto* mute=tool.widget->findChild<QCheckBox*>("fm-live-mute");volume->setValue(23);mute->setChecked(true);
    tool.sourceChanged({false,{}});require(!start->isEnabled(),"Ending capture left FM start enabled");
    tool.failure(std::make_exception_ptr(std::runtime_error("independent_diagnostic")));
    auto* error=tool.widget->findChild<QLabel*>("fm-live-error");require(error && !error->text().isEmpty(),"A real module error was reduced to a transient generic status");
    if(!screenshot.isEmpty()){QCoreApplication::processEvents();tool.widget->grab().save(screenshot);}
    delete tool.widget;
    auto again=provider.create("en",&owner);require(again.widget->findChild<QSlider*>("fm-live-volume")->value()==23 && again.widget->findChild<QCheckBox*>("fm-live-mute")->isChecked(),"FM output preferences were not retained");delete again.widget;
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("AMC Tests");app.setApplicationName("Live research");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try {
        if(app.arguments().contains("--resource-check")) {
            auto tool=fmLiveResearchProvider().create("nl-NL",nullptr);
            require(tool.widget->windowTitle()=="FM resource extension","A new language resource did not change the module title");
            require(tool.widget->findChild<QPushButton*>("fm-live-start")->text()=="Listen","Missing module translations did not fall back per key");delete tool.widget;return 0;
        }
        require(argc>=2,"Expected independent provider resources");providerExtension(QFileInfo(app.arguments()[1]).filesystemFilePath());
        independentLifecycle(QFileInfo(app.arguments()[1]).filesystemFilePath());fmControls(argc>2?app.arguments()[2]:QString());
        QTemporaryDir resources;const auto directory=QDir(resources.path()).filePath(QString::fromUtf8("资源 📡/fm-live/i18n"));require(QDir().mkpath(directory),"Could not create Unicode resource extension directory");
        const auto source=resource_directory("fm-live")/"i18n";
        for(const auto& entry:std::filesystem::directory_iterator(source))std::filesystem::copy_file(entry.path(),QFileInfo(QDir(directory).filePath(QString::fromStdString(path_to_utf8(entry.path().filename())))).filesystemFilePath());
        amc_test::write(QDir(directory).filePath("nl.json"),"{\"title\":\"FM resource extension\"}");
        amc_test::ChildRunner child(resources.path());child.setEnvironment("SIGNALSCOPE_RESOURCE_PATH",QFileInfo(directory).dir().absolutePath()+"/..");
        child.run(QCoreApplication::applicationFilePath(),{"--resource-check"});
        std::cout<<"Independent live provider, bounded failures, source epochs, generic event actions, View panels, FM validation and resource-only language extension passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
