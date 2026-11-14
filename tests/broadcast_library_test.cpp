#include "plugins/desktop/research/builtin/fm_live.hpp"
#include "plugins/desktop/research/broadcast_search.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
using namespace signalscope;using namespace signalscope::desktop;
namespace {
void require(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
void pump(int milliseconds){QElapsedTimer clock;clock.start();while(clock.elapsed()<milliseconds){QApplication::processEvents();QThread::msleep(5);}}
void favorites() {
    auto provider=fmLiveResearchProvider();auto tool=provider.create("zh-CN",nullptr);
    auto actions=tool.actions({{"evidence_level","broadcast_fm_stereo"},{"center_frequency_hz",98100000}});require(actions.size()==3,"FM event had no favorite action");actions.back().trigger();
    auto* list=tool.widget->findChild<QComboBox*>("fm-live-presets");auto* name=tool.widget->findChild<QLineEdit*>("fm-live-preset-name");auto* save=tool.widget->findChild<QPushButton*>("fm-live-preset-save");
    require(list && list->count()==1 && name && save,"Event favorite was not visible in the standalone FM library");
    name->setText(QString::fromUtf8("我的电台 📻"));save->click();require(list->count()==1 && list->currentText()==name->text(),"Naming a favorite duplicated or lost the station");
    tool.widget->findChild<QLineEdit*>("fm-live-frequency")->setText("90");actions.back().trigger();require(list->count()==1 && list->currentText()==QString::fromUtf8("我的电台 📻"),"Saving the same detection again overwrote a user's station name");
    delete tool.widget;
    auto reopened=provider.create("en",nullptr);list=reopened.widget->findChild<QComboBox*>("fm-live-presets");require(list->count()==1 && list->itemText(0)==QString::fromUtf8("我的电台 📻"),"Favorites did not survive reopening / language changes");
    list->setCurrentIndex(0);QMetaObject::invokeMethod(list,"activated",Q_ARG(int,0));
    auto* frequency=reopened.widget->findChild<QLineEdit*>("fm-live-frequency");require(frequency->text()=="98.100000","Selecting a favorite did not restore its frequency independently of monitoring");
    const auto args=QApplication::arguments();const auto image=args.indexOf("--screenshot");if(image>=0 && image+1<args.size()){reopened.widget->resize(480,820);reopened.widget->show();pump(100);require(reopened.widget->grab().save(args[image+1]),"Could not save the broadcast library screenshot");}
    frequency->setText("101.7");reopened.widget->findChild<QLineEdit*>("fm-live-preset-name")->setText("Second");reopened.widget->findChild<QPushButton*>("fm-live-preset-save")->click();require(list->count()==2,"A manual station overwrote another favorite");
    reopened.widget->findChild<QPushButton*>("fm-live-preset-remove")->click();require(list->count()==1,"Removing one favorite removed the library");delete reopened.widget;
    QSettings settings;settings.beginGroup("live-research/fm-audio");const auto value=Json::parse(settings.value("favorites_v1").toString().toUtf8().toStdString());require(value.at("version")==1 && value.at("stations")[0].at("value").at("frequency_hz")==98100000,"Station identity was replaced by a presentation value");
    const QString future="{\"version\":99,\"stations\":[],\"future_data\":\"keep\"}";settings.setValue("favorites_v1",future);settings.sync();
    auto unknown=provider.create("en",nullptr);require(!unknown.widget->findChild<QPushButton*>("fm-live-preset-save")->isEnabled(),"Unknown favorite versions were writable");delete unknown.widget;require(settings.value("favorites_v1").toString()==future,"Unknown user data was overwritten");
}
void search() {
    const auto catalog=TextCatalog::resource("fm-live");
    BroadcastSearch widget([&](const auto& key,const auto& args){Json value={{"label_key",key},{"arguments",args}};catalog.resolve(value);return localizedText(value.at("label"),"en");},"independent",100,100.1,.1,.5);
    unsigned requested=0,released=0;double current=0;Json selected;
    widget.startProbe=[&](double hz){current=hz;++requested;widget.ready();};widget.stopProbe=[&]{++released;};widget.candidates=[&]{return std::vector<Json>{{{"opaque_station",current}}};};widget.selected=[&](const auto& value){selected=value;};
    widget.findChild<QPushButton*>("independent-scan-start")->click();widget.hide();pump(1400);
    auto* results=widget.findChild<QComboBox*>("independent-scan-results");require(!widget.active() && requested==2 && released==2 && results->count()==2,"Hidden search did not finish its independent provider's sequence / release every probe");
    results->setCurrentIndex(1);QMetaObject::invokeMethod(results,"activated",Q_ARG(int,1));require(selected.at("opaque_station")==100100000,"Generic search altered an independent provider's opaque station value");
    widget.startProbe=[&](double){++requested;};widget.findChild<QPushButton*>("independent-scan-start")->click();widget.findChild<QPushButton*>("independent-scan-cancel")->click();const auto count=requested;widget.ready();pump(700);require(!widget.active() && requested==count,"A late ready callback restarted canceled search");
}
}
int main(int argc,char** argv){QApplication app(argc,argv);app.setOrganizationName("AMC Tests");app.setApplicationName("Broadcast library");QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());try{favorites();search();std::cout<<"Station favorites, Unicode names, reopen, removal, unknown-data preservation and independent cancellable search passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
