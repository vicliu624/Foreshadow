#include "desktop_menus.hpp"
#include "plugins/desktop/display/signal_display.hpp"
#include "test_process.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <array>

namespace {
auto enabledDetectors(const signalscope::desktop::SessionDetectors& detectors) {
    return std::array{detectors.lora,detectors.narrowband.fsk,detectors.narrowband.ook,
                      detectors.narrowband.ask,detectors.narrowband.broadcast_fm,detectors.snapshot.css,
                      detectors.snapshot.psk.bpsk,detectors.snapshot.psk.qpsk,
                      detectors.snapshot.four_fsk,detectors.snapshot.ofdm};
}
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QTemporaryDir settings;
    using amc_test::require;
    try {
        require(settings.isValid(),"Could not isolate plugin preferences");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        app.setOrganizationName("AMC Tests");
        app.setApplicationName("Plugin Manager");
        QSettings().setValue("detection/fm",false);
        QSettings().setValue("display/spectrum-waterfall",false);
        QSettings().setValue("display/spectrum",true);
        QMainWindow window;
        auto* tabs=new QTabWidget;
        window.setCentralWidget(tabs);
        const auto displays=signalscope::desktop::mountSignalDisplays(*tabs);
        signalscope::desktop::DesktopMenus menus;
        QString launched;
        QString changedPlugin;
        QString removedPlugin;
        bool externalEnabled=true;
        menus.mount(window,*tabs,displays,{{"iq-overview","I/Q overview"},{"lora-structure","LoRa structure"},
                    {"external.example","External example",true,true,true}},[&](const QString& id){launched=id;},{},
                    [&](const QString& id,bool enabled){changedPlugin=id;externalEnabled=enabled;},{},
                    [&](const QString& id,const QString&){removedPlugin=id;return true;});
        const auto top=window.menuBar()->actions();
        require(top.size()==2 && top[0]->menu() && top[1]->menu(),"Detection or Plugins menu missing");
        auto* detection=top[0]->menu();
        auto* fm=detection->findChild<QAction*>("detection/fm");
        require(fm && !fm->isChecked() && !menus.selection().narrowband.broadcast_fm,
                "Existing FM preference was not restored");
        fm->setChecked(true);
        const std::array ids{"lora","fsk","ook","ask","fm","css","bpsk","qpsk","four-fsk","ofdm"};
        for(std::size_t i=0;i<ids.size();++i) {
            auto* action=detection->findChild<QAction*>(QStringLiteral("detection/")+ids[i]);
            require(action && action->isChecked(),"Missing or disabled default detector");
            action->setChecked(false);
            const auto flags=enabledDetectors(menus.selection());
            for(std::size_t j=0;j<flags.size();++j)
                require(flags[j]==(i!=j),"Detection preference changed the wrong session flag");
            action->setChecked(true);
        }
        auto* manage=top[1]->menu()->actions().front();
        require(manage->objectName()=="manage-plugins","Plugin manager must be directly accessible");
        const auto pluginActions=top[1]->menu()->actions();
        require(pluginActions.size()==3 && pluginActions[2]->menu(),"Research plugin submenu missing");
        auto* waterfall=pluginActions[1]->menu()->findChild<QAction*>("display/waterfall");
        require(displays.size()==3 && waterfall && !waterfall->isChecked() &&
                tabs->isTabVisible(0) && !tabs->isTabVisible(1) && tabs->isTabVisible(2),
                "Display preferences did not inherit the combined setting or preserve the new preference");
        waterfall->setChecked(true);
        require(tabs->isTabVisible(1),"Waterfall could not be enabled independently");
        pluginActions[2]->menu()->actions().front()->trigger();
        require(launched=="iq-overview","Research menu dispatched the wrong plugin");
        auto* externalAction=pluginActions[2]->menu()->actions().last();

        for(const bool running:{false,true}) {
            menus.setSessionRunning(running);
            bool verified=false;
            QTimer::singleShot(0,&window,[&]{
                auto* dialog=window.findChild<QDialog*>("plugin-manager");
                if(!dialog)return;
                for(auto* checkbox:dialog->findChildren<QCheckBox*>()) {
                    if(checkbox->objectName()=="detection/lora") {
                        verified=checkbox->isEnabled()==!running && checkbox->isChecked()==!running;
                        if(running) {
                            menus.setSessionRunning(false);
                            verified=verified && checkbox->isEnabled();
                            menus.setSessionRunning(true);
                            verified=verified && !checkbox->isEnabled();
                        }
                        if(!running)checkbox->click();
                    }
                    if(!running && checkbox->objectName()=="display/waterfall")checkbox->click();
                }
                if(!running) {
                    auto* external=dialog->findChild<QCheckBox*>("research/external.example");
                    auto* launch=dialog->findChild<QPushButton*>("external.example");
                    if(external && launch) {
                        external->click();
                        verified=verified && !launch->isEnabled();
                    } else verified=false;
                    auto* button=dialog->findChild<QPushButton*>("lora-structure");
                    if(button){button->click();return;}
                }
                dialog->reject();
            });
            manage->trigger();
            require(verified,"Plugin manager detection selection or running-session lock is incorrect");
            require(!menus.selection().lora && !detection->findChild<QAction*>("detection/lora")->isChecked(),
                    "Detection menu and plugin manager disagree");
            require(tabs->isTabVisible(0) && !tabs->isTabVisible(1) && tabs->isTabVisible(2),
                    "Turning off the waterfall hid another display");
            require(launched=="lora-structure","Plugin manager did not open the selected research plugin");
            require(changedPlugin=="external.example" && !externalEnabled && !externalAction->isEnabled(),
                    "External plugin enabled state did not reach the menu and workspace callback");
        }
        const QSettings stored;
        require(!stored.value("detection/lora",true).toBool() && !stored.value("display/waterfall",true).toBool(),
                "Plugin preferences were not saved");
        QTimer::singleShot(0,&window,[&]{
            auto* dialog=window.findChild<QDialog*>("plugin-manager");
            if(!dialog)return;
            if(auto* uninstall=dialog->findChild<QPushButton*>("uninstall/external.example"))uninstall->click();
            else dialog->reject();
        });
        manage->trigger();
        require(removedPlugin=="external.example" && !externalAction->isVisible(),
                "Uninstall did not remove the managed research menu entry");
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
