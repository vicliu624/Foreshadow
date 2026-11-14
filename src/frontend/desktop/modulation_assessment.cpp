#include "modulation_assessment.hpp"
#include "receiver_probe.hpp"
#include "plugin_text.hpp"
#include "signalscope/adapters/modulation/descriptor.hpp"
#include <QCoreApplication>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace signalscope::desktop {
QList<ModulationAssessment> assessReceiverModulations(const ReceiverCapabilities& caps,const Config& cfg,const LoRaConfig& lc,
    bool lora,NarrowbandSelection narrowband,SnapshotSelection snapshots) {
    QList<ModulationAssessment> rows;
    DetectorSelection selected{lora,narrowband,snapshots};
    for(const auto& descriptor:modulationDescriptors()) {
        const bool isLoRa=descriptor.stage==DetectorStage::lora_stream;
        const bool isSnapshot=descriptor.stage==DetectorStage::snapshot;
        const auto p=planMonitorConfiguration(caps,cfg,lc,isLoRa,!isLoRa && !isSnapshot,isSnapshot);
        const DetectorContext context{cfg,lc,p.maximum_search_width_hz,p.lora_channels,p.lora_analysis_rate_hz};
        const auto data=descriptor.describe(context);
        ModulationAssessment row;row.id=QString::fromStdString(descriptor.id);row.name=pluginText(data.at("label"));
        const bool enabled=descriptor.enabled(selected);row.enabled=enabled;row.compatible=p.valid();row.covered=data.value("covered",true);
        const auto field=[&](const char* key){return pluginText(data.at(key).at("message"));};
        row.parsing=field("parsing");row.parsingSummary=field("parsing_summary");row.impactSummary=field("impact_summary");
        const auto effect=field("effect");QStringList reasons;
        for(const auto& issue:p.issues)if(issue.blocking)reasons<<configurationIssueText(issue);
        row.status=!enabled?QCoreApplication::translate("ModulationAssessment","Disabled"):
            !row.compatible?QCoreApplication::translate("ModulationAssessment","Blocked by settings"):
            !row.covered?QCoreApplication::translate("ModulationAssessment","Outside supported band"):
            QCoreApplication::translate("ModulationAssessment","Configuration compatible");
        if(!enabled)reasons<<QCoreApplication::translate("ModulationAssessment","Enable this detector in the Detection menu.");
        if(!row.covered && data.contains("coverage_reason"))reasons<<pluginText(data.at("coverage_reason").at("message"));
        reasons<<effect;
        if(p.valid())reasons<<QCoreApplication::translate("ModulationAssessment","Wideband coverage uses the acquisition span. Filter rolloff, incomplete edge signals, snapshot triggering and processing backlog can reduce results; compatible settings do not guarantee detection.");
        row.impact=reasons.join(u'\n');rows.push_back(std::move(row));
    }
    return rows;
}
}
