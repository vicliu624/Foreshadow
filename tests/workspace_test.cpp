#include "test_process.hpp"
#include "workspace.hpp"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
using namespace signalscope::desktop;
using amc_test::require;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    app.setOrganizationName("AMC Tests");
    app.setApplicationName("Workspace");
    try {
        {
            QMainWindow window;
            Workspace workspace(window);
            auto *first = new QTextEdit;
            auto *extra = new QTextEdit;
            workspace.add("first", "First pane", first, Qt::RightDockWidgetArea);
            workspace.add("independent-provider", "Independent pane", extra, Qt::RightDockWidgetArea);
            unsigned defaults = 0;
            workspace.restore([&](bool) {
                ++defaults;
                window.addDockWidget(Qt::RightDockWidgetArea, workspace.dock(first));
                window.addDockWidget(Qt::RightDockWidgetArea, workspace.dock(extra));
            });
            window.showMaximized();
            QCoreApplication::processEvents();
            require(defaults == 1, "A new workspace did not apply its initial layout");
            require(!workspace.restoredFromSettings(),"A default layout was reported as restored state");
            auto *toggle = window.findChild<QAction *>("view/independent-provider");
            require(toggle && toggle->isChecked(), "New provider pane did not automatically appear in View");
            toggle->trigger();
            require(!workspace.visible(extra), "View toggle did not hide pane");
            workspace.show(extra);
            require(toggle->isChecked(), "Opening a hidden pane did not update View");
            workspace.dock(first)->setFloating(true);
            workspace.setVisible(extra, false);
            workspace.save();
        }
        {
            QMainWindow window;
            Workspace workspace(window);
            auto *first = new QTextEdit;
            auto *extra = new QTextEdit;
            workspace.add("first", "First pane", first, Qt::RightDockWidgetArea);
            workspace.add("independent-provider", "Independent pane", extra, Qt::RightDockWidgetArea);
            unsigned defaults = 0;
            workspace.restore([&](bool) {
                ++defaults;
                window.addDockWidget(Qt::RightDockWidgetArea, workspace.dock(first));
                window.addDockWidget(Qt::RightDockWidgetArea, workspace.dock(extra));
            });
            require(window.isMaximized(),"Restoring dock state lost the saved maximized window state");
            window.show();
            QCoreApplication::processEvents();
            require(workspace.dock(first)->isFloating() && !workspace.visible(extra),
                    "Panel geometry/visibility was not restored across a new window");
            require(defaults == 0, "Restoring a saved workspace scheduled an unrelated preset layout");
            require(workspace.restoredFromSettings(),"A successful saved-state restore was not reported");
            workspace.reset(true);
            QCoreApplication::processEvents();
            require(!workspace.dock(first)->isFloating() && workspace.visible(extra),
                    "Reset did not recover closed/floating panes");
            require(defaults == 1, "Explicit reset did not apply the requested preset");
        }
        std::cout << "Independent panel discovery, View controls, floating, persistent layout and recovery passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
