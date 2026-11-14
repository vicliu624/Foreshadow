#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>
#include <map>
#include <vector>
class QAction;
class QDockWidget;
class QMainWindow;
class QMenu;
class QWidget;
namespace signalscope::desktop {
struct WorkspaceLayoutItem {
    QWidget* widget=nullptr;
    Qt::DockWidgetArea area=Qt::RightDockWidgetArea;
    QWidget* relativeTo=nullptr;
    Qt::Orientation orientation=Qt::Horizontal;
    bool tabbed=false;
};
class Workspace final : public QObject {
  public:
    explicit Workspace(QMainWindow &);
    QDockWidget *add(const QString &id, const QString &title, QWidget *, Qt::DockWidgetArea);
    void show(QWidget *);
    void place(QWidget *widget,QWidget *relative,Qt::Orientation,bool tabbed=false);
    void addLayout(const QString& id,const QString& title,std::function<void()>);
    void applyLayout(const std::vector<WorkspaceLayoutItem>&);
    void setVisible(QWidget *, bool);
    bool visible(QWidget *) const;
    QString title(QWidget *) const;
    void setTitle(QWidget *, const QString &);
    void observe(QWidget *, std::function<void(bool)>);
    QDockWidget *dock(QWidget *) const;
    void restore(std::function<void(bool compact)> defaultLayout);
    void save();
    bool compact() const { return compact_; }
    bool restoredFromSettings() const { return restored_; }
    void reset(bool compact);

  private:
    QMainWindow &window_;
    QMenu *view_;
    QMenu *layouts_;
    QAction *lock_;
    std::map<QWidget *, QDockWidget *> panes_;
    std::function<void(bool)> defaults_;
    bool compact_ = false;
    bool restored_ = false;
    QDockWidget *focused_ = nullptr;
    QByteArray baseLayout_;
};
} // namespace signalscope::desktop
