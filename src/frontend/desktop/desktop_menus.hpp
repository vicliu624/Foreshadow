#pragma once

#include "plugins/desktop/research/research_plugin.hpp"
#include "signalscope/adapters/modulation/detection_selection.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include "signalscope/adapters/modulation/descriptor.hpp"
#include <functional>
#include <span>
#include <QList>

class QMainWindow;
class QMenu;
class QTabWidget;
class QWidget;

namespace signalscope::desktop {
class SignalDisplay;

using SessionDetectors=DetectorSelection;

class DesktopMenus {
public:
    struct DisplayPanels {
        std::function<QString(QWidget*)> title;
        std::function<void(QWidget*,bool)> setVisible;
        std::function<void(QWidget*,std::function<void(bool)>)> observe;
    };
    void mount(QMainWindow& window,DisplayPanels panels,std::span<SignalDisplay* const> displays,
               const QList<ResearchPlugin>& researchPlugins,const std::function<void(const QString&)>& openResearch,
               const std::function<void()>& stopAudio={},const std::function<void(const QString&,bool)>& setResearchEnabled={},
               const std::function<void()>& installResearch={},const std::function<bool(const QString&,const QString&)>& removeResearch={},
               const std::function<void()>& manageToolboxes={});
    void mount(QMainWindow& window,QTabWidget& tabs,std::span<SignalDisplay* const> displays,
               const QList<ResearchPlugin>& researchPlugins,const std::function<void(const QString&)>& openResearch,
               const std::function<void()>& stopAudio={},
               const std::function<void(const QString&,bool)>& setResearchEnabled={},
               const std::function<void()>& installResearch={},
               const std::function<bool(const QString&,const QString&)>& removeResearch={},
               const std::function<void()>& manageToolboxes={});
    SessionDetectors selection() const;
    void setSessionRunning(bool running);

private:
    QMenu* detection_=nullptr;
    SessionDetectors selected_;
};
}
