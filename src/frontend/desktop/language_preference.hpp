#pragma once

#include <QStringList>
#include <QList>
#include <optional>

namespace signalscope::desktop {
struct LanguageSelection {
    QString mode;
    QString tag;
};
struct LanguagePack {
    QString tag,nativeName,catalog,qtCatalog;
    QStringList aliases;
    bool source=false;
};
QList<LanguagePack> languagePacks();

std::optional<LanguageSelection> resolveLanguageSelection(const QString& requested,const QString& saved,
                                                          const QStringList& systemLanguages);
}
