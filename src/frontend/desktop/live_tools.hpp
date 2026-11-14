#pragma once
#include "plugins/desktop/research/live_provider.hpp"
#include <QObject>
#include <atomic>
class QMenu;
namespace signalscope::desktop {
struct LiveResearchPanel {
    QString id,title;QWidget* widget;bool initiallyVisible;QString loadError;
    Qt::DockWidgetArea preferredArea=Qt::RightDockWidgetArea;
    QWidget* relativeTo=nullptr;
    Qt::Orientation orientation=Qt::Horizontal;
};
struct LiveResearchWorkspace {
    QString id,title;
    QWidget* primary=nullptr;
    std::vector<ResearchPanelDescriptor> panels;
};
class LiveTools final:public QObject {
public:
    LiveTools(std::vector<LiveResearchProvider>,const QString& language,QWidget* parent=nullptr);
    ~LiveTools() override;
    const std::vector<LiveResearchPanel>& panels() const {return panels_;}
    const std::vector<LiveResearchWorkspace>& layouts() const {return layouts_;}
    void setSession(bool receiving);
    IqBlockSubscriber subscriber();
    void addEventActions(QMenu&,const Json&);
    void addRecordingActions(QMenu&,const ResearchRecordingContext&);
    void stopAll();
    void setReceptionAcquire(ReceptionAcquire,std::function<void()> receiverControls={});
    QString summary() const;
    QString health() const;
    bool showHealth();
    bool busy() const;
    Json diagnostics() const;
    std::function<void(QWidget*)> showPanel;
private:
    struct Dispatch;
    std::shared_ptr<Dispatch> dispatch_;
    void deliver(const IqBlockView&);
    void addActions(QMenu&,std::size_t,std::vector<LiveResearchAction>);
    void reportFailure(std::size_t,std::exception_ptr,std::uint64_t) const;
    std::vector<LiveResearchTool> tools_;
    std::vector<std::unique_ptr<std::atomic<bool>>> faulted_;
    std::vector<LiveResearchPanel> panels_;
    std::vector<LiveResearchWorkspace> layouts_;
    std::vector<std::size_t> primary_panels_;
    QString language_;
    mutable std::vector<QString> failures_;
    std::vector<std::shared_ptr<std::atomic<std::uint64_t>>> configurationWaitPairs_;
    std::atomic<std::uint64_t> generation_=0;
    std::atomic<bool> awaitingStream_=false,receiving_=false;
};
}
