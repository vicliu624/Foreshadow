#pragma once
#include "signalscope/backend/iq/stream.hpp"
#include "signalscope/backend/iq/reception.hpp"
#include "plugins/desktop/research/research_plugin.hpp"
#include <QString>
#include <QWidget>
#include <optional>
#include <exception>
#include <functional>
#include <vector>

namespace signalscope::desktop {
inline constexpr unsigned liveResearchProtocolVersion=3;
inline constexpr bool supportsLiveResearchProtocol(unsigned version){return version>=1 && version<=liveResearchProtocolVersion;}
struct LiveResearchContext {
    bool receiving=false;
    std::optional<IqStreamParameters> stream;
};
struct LiveResearchAction {
    QString id,label,unavailableReason;
    bool enabled=true;
    std::function<void()> trigger;
    // v3: additional panels owned and declared by this same provider.
    std::vector<QWidget*> reveal;
};
struct ResearchRecordingContext {
    std::filesystem::path metadata;
    // File-relative sample pairs; source-clock conversion belongs to the
    // shared recording/window interface, not to a menu or an event ID table.
    std::optional<ResearchSampleRange> range;
    Json event=Json::object();
};
struct ResearchPanelDescriptor {
    QString id,title;
    QWidget* widget=nullptr;
    bool initiallyVisible=false;
    Qt::DockWidgetArea preferredArea=Qt::RightDockWidgetArea;
    QWidget* relativeTo=nullptr;
    Qt::Orientation orientation=Qt::Horizontal;
};
struct LiveResearchTool {
    QWidget* widget=nullptr;
    // sourceChanged, actions, stop and busy run on the UI thread. offer runs
    // on the capture thread, must return promptly, and must copy borrowed I/Q
    // before retaining it. Each tool owns its queues and continuity metrics.
    std::function<void(const LiveResearchContext&)> sourceChanged;
    IqBlockSubscriber offer;
    std::function<std::vector<LiveResearchAction>(const Json&)> actions;
    std::function<void()> stop;
    std::function<bool()> busy;
    std::function<void(std::exception_ptr)> failure;
    bool initiallyVisible=false;
    // Optional provider-owned diagnostic payload, sampled on the UI thread.
    // Consumers preserve it without interpreting implementation-specific keys.
    std::function<Json()> snapshot;
    // v2: request a receiver independently of monitor. The host marshals
    // status to the UI thread; I/Q remains borrowed on the capture thread.
    // v1 providers leave this empty and retain the monitor-bound lifecycle.
    std::function<void(ReceptionAcquire)> attachReception;
    // Provider-owned localized summary; generic status surfaces do not parse
    // snapshots or branch on modulation identifiers.
    std::function<QString()> summary;
    std::function<QString()> health;
    // Optional host navigation for a self-contained reception panel. The
    // provider never imports the receiver dialog or opens a concrete driver.
    std::function<void(std::function<void()>)> attachReceiverControls;
    // v3: the host adopts these widgets through the generic workspace.
    // All descriptors and callbacks remain provider-owned. Neither this
    // capability nor native widgets are added to the external snapshot IPC.
    std::vector<ResearchPanelDescriptor> panels;
    std::function<std::vector<LiveResearchAction>(const ResearchRecordingContext&)> recordingActions;
    // v3: a file/frozen research job can outlive monitor start/stop. Explicit
    // stopAll still stops it. v1/v2 retain their existing linked behavior.
    bool monitorBound=true;
    // Optional, provider-localized explicit workspace preset. Showing an
    // ordinary panel does not apply a preset or discard a user's layout.
    QString workspaceLayoutTitle;
};
struct LiveResearchProvider {
    QString id;
    std::function<LiveResearchTool(const QString& language,QWidget* parent)> create;
    unsigned protocolVersion=liveResearchProtocolVersion;
};
std::vector<LiveResearchProvider> liveResearchProviders();
void registerLiveResearchProvider(LiveResearchProvider);
}
