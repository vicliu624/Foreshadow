#pragma once
#include <QLocale>
#include <QStringList>
#include <nlohmann/json.hpp>
#include "signalscope/backend/iq/locale.hpp"
namespace signalscope::desktop {
inline QStringList languageFallbacks(QString tag,bool includeSource=true) {
    tag.replace(u'_',u'-');QStringList result{tag};
    result.append(QLocale(tag).uiLanguages());
    const QLocale canonical(tag);
    const auto language=QLocale::languageToCode(canonical.language());
    const auto script=QLocale::scriptToCode(canonical.script());
    if(!language.isEmpty() && !script.isEmpty())result.push_back(language+u'-'+script);
    for(const auto& candidate:locale_fallbacks(tag.toStdString(),includeSource))result.push_back(QString::fromStdString(candidate));
    result.removeDuplicates();return result;
}
inline QString localizedText(const nlohmann::json& text,const QString& tag,const QString& fallback={}) {
    std::vector<std::string> candidates;for(const auto& locale:languageFallbacks(tag))candidates.push_back(locale.toStdString());
    return QString::fromStdString(localized_value(text,candidates,fallback.toStdString()));
}
}
