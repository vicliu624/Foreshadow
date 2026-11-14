#include "workspace.hpp"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QTabWidget>
#include <algorithm>
#include <stdexcept>
#include <set>
namespace signalscope::desktop {

Workspace::Workspace(QMainWindow &window) : QObject(&window), window_(window) {
    window.setDockNestingEnabled(true);
    window.setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks |
                          QMainWindow::GroupedDragging);
    window.setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    window.setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    window.setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    view_ = window.menuBar()->addMenu(QCoreApplication::translate("Workspace", "View"));
    view_->setObjectName("workspace-view-menu");
    auto *layouts = view_->addMenu(QCoreApplication::translate("Workspace", "Layout"));
    layouts_=layouts;
    auto *monitor = layouts->addAction(QCoreApplication::translate("Workspace", "Monitoring layout"), this,
                                       [this] { reset(false); });
    monitor->setObjectName("workspace-monitor-layout");
    auto *compact =
        layouts->addAction(QCoreApplication::translate("Workspace", "Compact layout"), this, [this] { reset(true); });
    compact->setObjectName("workspace-compact-layout");
    layouts->addAction(QCoreApplication::translate("Workspace", "Save layout"), this, [this] { save(); });
    auto *resetAction = layouts->addAction(QCoreApplication::translate("Workspace", "Restore default layout"), this,
                                           [this] { reset(window_.width() < 1200 || window_.height() < 720); });
    resetAction->setObjectName("workspace-reset-layout");
    lock_ = layouts->addAction(QCoreApplication::translate("Workspace", "Lock panel positions"));
    lock_->setCheckable(true);
    QObject::connect(lock_, &QAction::toggled, this, [this](bool locked) {
        for (const auto &[widget, pane] : panes_)
            pane->setFeatures(locked ? QDockWidget::DockWidgetClosable
                                     : QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                                           QDockWidget::DockWidgetFloatable);
    });
    view_->addAction(QCoreApplication::translate("Workspace", "Float focused panel"), this, [this] {
        if (focused_ && focused_->features().testFlag(QDockWidget::DockWidgetFloatable))
            focused_->setFloating(!focused_->isFloating());
    });
    QObject::connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *next) {
        for (auto *widget = next; widget; widget = widget->parentWidget())
            if (auto *pane = qobject_cast<QDockWidget *>(widget)) {
                focused_ = pane;
                break;
            }
    });
    view_->addSeparator();
}
QDockWidget *Workspace::add(const QString &id, const QString &title, QWidget *widget, Qt::DockWidgetArea area) {
    if (id.isEmpty() || panes_.contains(widget) || window_.findChild<QDockWidget *>("pane/" + id))
        throw std::invalid_argument("Duplicate workspace pane");
    auto *pane = new QDockWidget(title, &window_);
    pane->setObjectName("pane/" + id);
    pane->setAllowedAreas(Qt::AllDockWidgetAreas);
    pane->setWidget(widget);
    window_.addDockWidget(area, pane);
    panes_[widget] = pane;
    auto *action = pane->toggleViewAction();
    action->setObjectName("view/" + id);
    view_->addAction(action);
    return pane;
}
QDockWidget *Workspace::dock(QWidget *widget) const { return panes_.at(widget); }
void Workspace::addLayout(const QString& id,const QString& title,std::function<void()> apply) {
    const auto name="workspace-layout/"+id;
    if(id.isEmpty() || title.isEmpty() || !apply || window_.findChild<QAction*>(name))throw std::invalid_argument("Incomplete or duplicate workspace layout");
    auto* action=layouts_->addAction(title,this,[this,apply=std::move(apply)]{lock_->setChecked(false);apply();});action->setObjectName(name);
}
void Workspace::applyLayout(const std::vector<WorkspaceLayoutItem>& items) {
    std::set<QWidget*> declared;
    for(const auto& item:items) {
        if(!panes_.contains(item.widget) || !declared.insert(item.widget).second ||
            (item.area!=Qt::LeftDockWidgetArea && item.area!=Qt::RightDockWidgetArea && item.area!=Qt::TopDockWidgetArea && item.area!=Qt::BottomDockWidgetArea) ||
            (item.orientation!=Qt::Horizontal && item.orientation!=Qt::Vertical) ||
            (item.relativeTo && (item.relativeTo==item.widget || !declared.contains(item.relativeTo))))throw std::invalid_argument("Invalid workspace layout panel or ordering");
    }
    if(items.empty())throw std::invalid_argument("A workspace layout needs at least one panel");
    lock_->setChecked(false);
    for(const auto& [widget,pane]:panes_){pane->setFloating(false);window_.removeDockWidget(pane);pane->hide();}
    for(const auto& item:items){auto* pane=dock(item.widget);window_.addDockWidget(item.area,pane);if(item.relativeTo)place(item.widget,item.relativeTo,item.orientation,item.tabbed);pane->show();}
    dock(items.front().widget)->raise();
}
void Workspace::place(QWidget *widget,QWidget *relative,Qt::Orientation orientation,bool tabbed) {
    if(widget==relative)throw std::invalid_argument("A workspace pane cannot be placed relative to itself");
    if(tabbed)window_.tabifyDockWidget(dock(relative),dock(widget));
    else {
        auto* anchor=dock(relative);
        // Qt adds a new tab instead of splitting when the anchor is tabbed.
        // Placement runs only during assembly or an explicit layout preset;
        // saved layouts are restored afterwards and ordinary resize never
        // invokes it.
        if(!window_.tabifiedDockWidgets(anchor).empty()) {
            const auto area=window_.dockWidgetArea(anchor);window_.removeDockWidget(anchor);
            window_.addDockWidget(area==Qt::NoDockWidgetArea?Qt::RightDockWidgetArea:area,anchor);
        }
        window_.splitDockWidget(anchor,dock(widget),orientation);
    }
}
void Workspace::show(QWidget *widget) {
    auto *pane = dock(widget);
    pane->show();
    pane->raise();
    widget->setFocus(Qt::OtherFocusReason);
}
void Workspace::setVisible(QWidget *widget, bool visible) { dock(widget)->setVisible(visible); }
bool Workspace::visible(QWidget *widget) const { return !dock(widget)->isHidden(); }
QString Workspace::title(QWidget *widget) const { return dock(widget)->windowTitle(); }
void Workspace::setTitle(QWidget *widget, const QString &title) { dock(widget)->setWindowTitle(title); }
void Workspace::observe(QWidget *widget, std::function<void(bool)> changed) {
    auto *pane = dock(widget);
    QObject::connect(pane, &QDockWidget::visibilityChanged, this,
                     [pane, changed = std::move(changed)](bool) { changed(!pane->isHidden()); });
}
void Workspace::reset(bool compact) {
    if (!defaults_)
        return;
    lock_->setChecked(false);
    compact_ = compact;
    window_.restoreState(baseLayout_, 1);
    for (const auto &[widget, pane] : panes_) {
        pane->setFloating(false);
        pane->show();
    }
    defaults_(compact);
}
void Workspace::restore(std::function<void(bool)> defaults) {
    defaults_ = std::move(defaults);
    baseLayout_ = window_.saveState(1);
    const auto area = QGuiApplication::primaryScreen()->availableGeometry();
    QSettings settings;
    const bool geometry = window_.restoreGeometry(settings.value("workspace/v1/geometry").toByteArray());
    if (!geometry) {
        window_.resize(std::min(1680, area.width() - 24), std::min(1000, area.height() - 48));
        if (area.width() >= 1600 && area.height() >= 900)
            window_.setWindowState(Qt::WindowMaximized);
    }
    compact_ = window_.width() < 1200 || window_.height() < 720;
    const auto savedState = settings.value("workspace/v1/state").toByteArray();
    // Restore before the first layout pass, exactly once. Running the default
    // dock rearrangement and a second queued restore invalidates Qt's pending
    // layout items on restart with a saved split/tab layout.
    // Qt 6.8 retains a shallow copy of a hidden, maximized window's restored
    // dock items until the native window manager finishes resizing it. Removing
    // a dock for an explicit preset can then leave that copy with deleted items.
    // Restore once in the normal state, before show(), and reinstate the saved
    // window state afterwards so later presets have only one live dock tree.
    const auto windowState = window_.windowState();
    window_.setWindowState(Qt::WindowNoState);
    restored_ = !savedState.isEmpty() && window_.restoreState(savedState, 1);
    if (!restored_) {
        window_.restoreState(baseLayout_, 1);
        defaults_(compact_);
    }
    window_.setWindowState(windowState);
    lock_->setChecked(settings.value("workspace/v1/locked", false).toBool());
    for (const auto &[widget, pane] : panes_) {
        if (pane->isFloating()) {
            const auto screen = QGuiApplication::screenAt(pane->frameGeometry().center());
            if (!screen)
                pane->setFloating(false);
        }
    }
}
void Workspace::save() {
    QSettings settings;
    settings.setValue("workspace/v1/geometry", window_.saveGeometry());
    settings.setValue("workspace/v1/state", window_.saveState(1));
    settings.setValue("workspace/v1/locked", lock_->isChecked());
}
} // namespace signalscope::desktop
