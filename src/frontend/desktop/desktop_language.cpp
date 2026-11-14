#include "desktop_language.hpp"
#include "language_preference.hpp"

#include <QActionGroup>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>

namespace signalscope::desktop {
namespace {
constexpr auto settings_key = "desktop/language";
}

bool Language::install(const QString& requested) {
    const auto selected=resolveLanguageSelection(requested,QSettings().value(settings_key).toString(),
                                                  QLocale::system().uiLanguages());
    if(!selected)return false;
    mode_=selected->mode;
    tag_=selected->tag;
    QCoreApplication::instance()->setProperty("signalscope-language",tag_);
    for(const auto& pack:languagePacks())if(pack.tag==tag_) {
        if(pack.source)return true;
        if(!application_.load(pack.catalog))return false;
        if(!pack.qtCatalog.isEmpty() && (qt_.load(":/i18n/"+pack.qtCatalog+".qm") ||
            qt_.load(pack.qtCatalog,QFileInfo(pack.catalog).absolutePath()) ||
            qt_.load(pack.qtCatalog,QLibraryInfo::path(QLibraryInfo::TranslationsPath))))QCoreApplication::installTranslator(&qt_);
        return QCoreApplication::installTranslator(&application_);
    }
    return false;
}

void Language::add_menu(QMainWindow& window) const {
    auto* menu = window.menuBar()->addMenu(QCoreApplication::translate("DesktopLanguage", "Language"));
    auto* group = new QActionGroup(menu);
    QList<std::pair<QString,QString>> choices{{QCoreApplication::translate("DesktopLanguage","Follow system language"),QStringLiteral("system")}};
    for(const auto& pack:languagePacks())choices.push_back({pack.nativeName,pack.tag});
    for(const auto& [label,tag]:choices) {
        auto* action = menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(tag == mode_);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, &window, [&window, tag, activeMode=mode_] {
            QSettings settings;
            if(settings.value(settings_key).toString()!=tag)settings.setValue(settings_key,tag);
            if(tag==activeMode)return;
            QMessageBox::information(&window,
                QCoreApplication::translate("DesktopLanguage", "Language"),
                QCoreApplication::translate("DesktopLanguage", "Restart Foreshadow to apply the selected language."));
        });
    }
}
}
