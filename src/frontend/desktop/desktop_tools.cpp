#include "desktop_tools.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace signalscope::desktop {
QString companion_cli() {
    const QDir directory(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    return directory.absoluteFilePath("signalscope.exe");
#elif defined(Q_OS_MACOS)
    const auto bundled=directory.absoluteFilePath("signalscope");
    if(QFileInfo(bundled).isExecutable())return bundled;
    return QDir::cleanPath(directory.absoluteFilePath("../../../signalscope"));
#else
    return directory.absoluteFilePath("signalscope");
#endif
}

}
