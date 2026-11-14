#include "src/frontend/desktop/legal_notices.hpp"
#include "src/frontend/desktop/desktop_theme.hpp"
#include <QApplication>
#include <QDialog>
#include <QTextBrowser>
#include <QTimer>
#include <QFileInfo>
#include <iostream>

// Run a copy from the extracted package's bin directory, so the real desktop
// material lookup, relative links and native rich-text renderer are exercised.
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    signalscope::desktop::applyDesktopTheme(app);
    if(app.arguments().size()!=3)return 2;
    const auto expectedCommit=app.arguments().at(1),screenshot=app.arguments().at(2);
    signalscope::desktop::showLegalNotices(nullptr);
    QTimer::singleShot(250,&app,[&]{
        for(auto* widget:QApplication::topLevelWidgets()) {
            auto* dialog=qobject_cast<QDialog*>(widget);
            if(!dialog || dialog->objectName()!="licenses-and-source")continue;
            auto* browser=dialog->findChild<QTextBrowser*>();
            const auto text=browser?browser->toPlainText():QString{};
            if(!browser || !text.contains(expectedCommit) || !text.contains("LGPL-3.0-only") ||
               !text.contains("GPL-3.0-or-later") || !text.contains("Unresolved conditions") ||
               !QFileInfo::exists(browser->source().resolved(QUrl("LICENSING.md")).toLocalFile()) ||
               !dialog->grab().save(screenshot)) {
                std::cerr<<"Packaged license view, source identity or local material links failed\n";
                app.exit(1);return;
            }
            std::cout<<"Packaged native license view rendered the expected source and component terms\n";
            app.exit(0);return;
        }
        std::cerr<<"No packaged license dialog appeared\n";app.exit(1);
    });
    return app.exec();
}
