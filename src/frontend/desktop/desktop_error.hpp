#pragma once

#include <exception>
#include <QString>

class QLabel;

namespace signalscope::desktop {
QString error_summary(const std::exception& error,const QString& fallback);
void show_error(QLabel& label, const std::exception& error, const QString& fallback);
}
