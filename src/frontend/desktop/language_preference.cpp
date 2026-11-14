#include "language_preference.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace signalscope::desktop {
QList<LanguagePack> languagePacks() {
    const QDir directory(QString::fromStdString(path_to_utf8(resource_directory("desktop")/"languages")));
    QList<LanguagePack> result;
    for(const auto& file:directory.entryList({"*.json"},QDir::Files)) {
        QFile input(directory.filePath(file));if(!input.open(QIODevice::ReadOnly))continue;
        const auto data=QJsonDocument::fromJson(input.readAll()).object();
        const auto tag=data.value("tag").toString();if(tag.isEmpty())continue;
        auto catalog=data.value("catalog").toString();
        if(!catalog.isEmpty() && !catalog.startsWith(":/"))catalog=directory.filePath(catalog);
        LanguagePack pack{tag,data.value("native_name").toString(tag),catalog,data.value("qt_catalog").toString(),{},data.value("source_language").toBool()};
        for(const auto& alias:data.value("aliases").toArray())pack.aliases.push_back(alias.toString());
        result.push_back(std::move(pack));
    }
    return result;
}
std::optional<LanguageSelection> resolveLanguageSelection(const QString& requested,const QString& saved,const QStringList& systemLanguages) {
    const auto packs=languagePacks();if(packs.isEmpty())return {};
    const auto preference=requested.isEmpty()?saved:requested;
    if(preference!="system" && !preference.isEmpty()) {
        // Explicit selections must identify an installed pack, not merely fall
        // through to the source language for an arbitrary invalid identifier.
        for(const auto& pack:packs)if(preference.compare(pack.tag,Qt::CaseInsensitive)==0 || pack.aliases.contains(preference,Qt::CaseInsensitive))
            return LanguageSelection{pack.tag,pack.tag};
        if(!requested.isEmpty())return {};
    }
    for(const auto& request:systemLanguages) {
        // Try this system preference's actual language chain before the next
        // preference; source fallback is applied only after all are examined.
        const auto candidates=languageFallbacks(request,false);
        for(const auto& candidate:candidates)for(const auto& pack:packs)
            if(candidate.compare(pack.tag,Qt::CaseInsensitive)==0 || pack.aliases.contains(candidate,Qt::CaseInsensitive))return LanguageSelection{"system",pack.tag};
    }
    for(const auto& pack:packs)if(pack.source)return LanguageSelection{"system",pack.tag};
    return LanguageSelection{"system",packs.front().tag};
}
}
