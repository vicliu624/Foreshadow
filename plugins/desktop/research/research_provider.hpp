#pragma once
#include "external_research.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <functional>
#include <QWidget>
namespace signalscope::desktop {
struct ResearchOptions {
    std::optional<ResearchSampleRange> range;
    Json parameters=Json::object();
};
struct ResearchControls {
    QWidget* widget=nullptr; // Parent-owned; callbacks remain owned by the host.
    std::function<Json()> read;
    std::function<void(const Json&)> setContext;
};
struct ResearchProvider {
    ResearchPlugin plugin;
    std::function<ResearchResult(const QString&,double,std::stop_token,const ResearchOptions&)> render;
    bool supportsRange=false;
    std::function<ResearchControls(QWidget*)> controls;
    QString runLabel;
};
std::vector<ResearchProvider> researchProviders(const QList<ExternalResearchPlugin>&);
void registerResearchProvider(ResearchProvider);
}
