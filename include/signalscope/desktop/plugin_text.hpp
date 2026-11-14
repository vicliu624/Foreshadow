#pragma once
#include <QCoreApplication>
#include "signalscope/desktop/localization.hpp"

namespace signalscope::desktop {
// Catalogs are owned by the plugin. Language selection and fallback are shared
// by all plugin text; adding a locale needs a resource file, not a code branch.
inline QString pluginText(const nlohmann::json& text,const QString& fallback={}) {
    auto locale=QCoreApplication::instance()->property("signalscope-language").toString();
    if(locale.isEmpty())locale=QLocale().bcp47Name();
    return localizedText(text,locale,fallback);
}
}
