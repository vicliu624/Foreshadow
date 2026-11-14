#pragma once

#include <QTranslator>

class QMainWindow;

namespace signalscope::desktop {
class Language {
public:
    bool install(const QString& requested);
    void add_menu(QMainWindow& window) const;
    const QString& tag() const { return tag_; }

private:
    QString tag_;
    QString mode_;
    QTranslator application_;
    QTranslator qt_;
};
}
