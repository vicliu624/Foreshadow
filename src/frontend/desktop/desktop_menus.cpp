#include "desktop_menus.hpp"
#include "plugin_text.hpp"
#include "plugins/desktop/display/signal_display.hpp"

#include <QCoreApplication>
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace signalscope::desktop {
namespace {
QAction* preference(QMenu& menu,const QString& label,const QString& key) {
    auto* action=menu.addAction(label);
    action->setCheckable(true);
    action->setChecked(QSettings().value(key,true).toBool());
    action->setObjectName(key);
    QObject::connect(action,&QAction::toggled,action,[key](bool value){QSettings().setValue(key,value);});
    return action;
}

void addPluginOptions(QVBoxLayout& layout,const QString& title,QMenu& menu) {
    auto* group=new QGroupBox(title);
    auto* options=new QVBoxLayout(group);
    const auto updateEnabled=[group,&menu]{group->setEnabled(menu.isEnabled());};
    QObject::connect(menu.menuAction(),&QAction::changed,group,updateEnabled);
    updateEnabled();
    for(auto* action:menu.actions()) {
        auto* checkbox=new QCheckBox(action->text(),group);
        checkbox->setObjectName(action->objectName());
        checkbox->setChecked(action->isChecked());
        QObject::connect(checkbox,&QCheckBox::toggled,action,&QAction::setChecked);
        QObject::connect(action,&QAction::toggled,checkbox,&QCheckBox::setChecked);
        options->addWidget(checkbox);
    }
    layout.addWidget(group);
}
}

void DesktopMenus::mount(QMainWindow& window,QTabWidget& tabs,std::span<SignalDisplay* const> displays,
                         const QList<ResearchPlugin>& researchPlugins,const std::function<void(const QString&)>& openResearch,
                         const std::function<void()>& stopAudio,
                         const std::function<void(const QString&,bool)>& setResearchEnabled,
                         const std::function<void()>& installResearch,
                         const std::function<bool(const QString&,const QString&)>& removeResearch,
                         const std::function<void()>& manageToolboxes) {
    mount(window,{[&tabs](QWidget* widget){return tabs.tabText(tabs.indexOf(widget));},
        [&tabs](QWidget* widget,bool visible){tabs.setTabVisible(tabs.indexOf(widget),visible);},{}},displays,
        researchPlugins,openResearch,stopAudio,setResearchEnabled,installResearch,removeResearch,manageToolboxes);
}
void DesktopMenus::mount(QMainWindow& window,DisplayPanels panels,std::span<SignalDisplay* const> displays,
                         const QList<ResearchPlugin>& researchPlugins,const std::function<void(const QString&)>& openResearch,
                         const std::function<void()>& stopAudio,const std::function<void(const QString&,bool)>& setResearchEnabled,
                         const std::function<void()>& installResearch,const std::function<bool(const QString&,const QString&)>& removeResearch,
                         const std::function<void()>& manageToolboxes) {
    detection_=window.menuBar()->addMenu(QCoreApplication::translate("DesktopMenus","Detection"));
    for(const auto& descriptor:modulationDescriptors()) {
        const auto data=descriptor.describe(DetectorContext{});
        auto* action=preference(*detection_,pluginText(data.at("label")),QStringLiteral("detection/")+QString::fromStdString(descriptor.preference_key));
        descriptor.enabled(selected_)=action->isChecked();
        QObject::connect(action,&QAction::toggled,action,[this,enabled=descriptor.enabled](bool checked){enabled(selected_)=checked;});
    }

    auto* plugins=window.menuBar()->addMenu(QCoreApplication::translate("DesktopMenus","Plugins"));
    if(manageToolboxes){auto* center=plugins->addAction(QCoreApplication::translate("DesktopMenus","Toolbox Center…"),&window,manageToolboxes);center->setObjectName("toolbox-center");}
    auto* manage=plugins->addAction(QCoreApplication::translate("DesktopMenus","Manage plugins…"));
    manage->setObjectName("manage-plugins");
    auto* displayMenu=plugins->addMenu(QCoreApplication::translate("DesktopMenus","Display plugins"));
    for(auto* display:displays) {
        const auto name=panels.title(display);
        auto* action=preference(*displayMenu,name,"display/"+display->objectName());
        panels.setVisible(display,action->isChecked());
        QObject::connect(action,&QAction::toggled,&window,[panels,display](bool enabled){panels.setVisible(display,enabled);});
        if(panels.observe)panels.observe(display,[action](bool visible){const QSignalBlocker blocker(action);action->setChecked(visible);});
    }
    auto* researchMenu=plugins->addMenu(QCoreApplication::translate("DesktopMenus","Research plugins"));
    for(const auto& plugin:researchPlugins) {
        auto* action=researchMenu->addAction(plugin.title,&window,[openResearch,id=plugin.id]{openResearch(id);});
        action->setData(plugin.id);
        action->setProperty("external",plugin.external);
        action->setProperty("managed",plugin.managed);
        action->setProperty("toolbox",plugin.toolboxId);
        action->setEnabled(plugin.enabled);
    }
    QObject::connect(manage,&QAction::triggered,&window,[this,&window,displayMenu,researchMenu,setResearchEnabled,installResearch,removeResearch,manageToolboxes]{
        QDialog dialog(&window);
        dialog.setObjectName("plugin-manager");
        dialog.setWindowTitle(QCoreApplication::translate("DesktopMenus","Manage plugins…"));
        auto* layout=new QVBoxLayout(&dialog);
        auto* categories=new QTabWidget(&dialog);
        layout->addWidget(categories);
        auto* detectionPage=new QWidget;
        auto* detectionLayout=new QVBoxLayout(detectionPage);
        addPluginOptions(*detectionLayout,detection_->title(),*detection_);
        auto* notice=new QLabel(QCoreApplication::translate("DesktopMenus","Stop the current session to change detection plugins."));
        notice->setWordWrap(true);
        const auto updateNotice=[this,notice]{notice->setVisible(!detection_->isEnabled());};
        QObject::connect(detection_->menuAction(),&QAction::changed,notice,updateNotice);
        updateNotice();
        detectionLayout->addWidget(notice);
        detectionLayout->addStretch();
        categories->addTab(detectionPage,detection_->title());
        auto* displayPage=new QWidget;
        auto* displayLayout=new QVBoxLayout(displayPage);
        addPluginOptions(*displayLayout,displayMenu->title(),*displayMenu);
        displayLayout->addStretch();
        categories->addTab(displayPage,displayMenu->title());
        auto* researchPage=new QWidget;
        auto* researchLayout=new QVBoxLayout(researchPage);
        if(installResearch) {
            auto* install=new QPushButton(QCoreApplication::translate("DesktopMenus","Install research plugin…"));
            QObject::connect(install,&QPushButton::clicked,&dialog,[&dialog,installResearch]{
                dialog.accept();
                installResearch();
            });
            researchLayout->addWidget(install);
        }
        for(auto* action:researchMenu->actions()) {
            if(!action->isVisible())continue;
            auto* row=new QHBoxLayout;
            auto* button=new QPushButton(action->text());
            button->setObjectName(action->data().toString());
            button->setEnabled(action->isEnabled());
            QObject::connect(action,&QAction::changed,button,[button,action]{button->setEnabled(action->isEnabled());});
            QObject::connect(button,&QPushButton::clicked,&dialog,[&dialog,action]{
                dialog.accept();
                action->trigger();
            });
            row->addWidget(button,1);
            if(!action->property("toolbox").toString().isEmpty() && manageToolboxes) {
                auto* toolbox=new QPushButton(QCoreApplication::translate("DesktopMenus","Manage toolbox…"));
                QObject::connect(toolbox,&QPushButton::clicked,&dialog,[&dialog,manageToolboxes]{dialog.accept();manageToolboxes();});row->addWidget(toolbox);
            }else if(action->property("external").toBool() && setResearchEnabled) {
                auto* enabled=new QCheckBox(QCoreApplication::translate("DesktopMenus","Enabled"));
                enabled->setObjectName(QStringLiteral("research/")+action->data().toString());
                enabled->setChecked(action->isEnabled());
                QObject::connect(enabled,&QCheckBox::toggled,&dialog,[action,setResearchEnabled](bool value){
                    setResearchEnabled(action->data().toString(),value);
                    action->setEnabled(value);
                });
                row->addWidget(enabled);
            }
            if(action->property("managed").toBool() && removeResearch) {
                auto* remove=new QPushButton(QCoreApplication::translate("DesktopMenus","Uninstall"));
                remove->setObjectName(QStringLiteral("uninstall/")+action->data().toString());
                QObject::connect(remove,&QPushButton::clicked,&dialog,[&dialog,action,removeResearch]{
                    dialog.accept();
                    if(removeResearch(action->data().toString(),action->text())) {
                        action->setEnabled(false);
                        action->setVisible(false);
                    }
                });
                row->addWidget(remove);
            }
            researchLayout->addLayout(row);
        }
        researchLayout->addStretch();
        auto* researchScroll=new QScrollArea;
        researchScroll->setWidgetResizable(true);
        researchScroll->setWidget(researchPage);
        categories->addTab(researchScroll,researchMenu->title());
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    });
    if(stopAudio)plugins->addAction(QCoreApplication::translate("DesktopMenus","Stop live tools"),&window,stopAudio);
}

SessionDetectors DesktopMenus::selection() const {
    return selected_;
}

void DesktopMenus::setSessionRunning(bool running) {detection_->setEnabled(!running);}
}
