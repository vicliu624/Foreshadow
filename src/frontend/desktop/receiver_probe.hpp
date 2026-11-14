#pragma once

#include <optional>
#include <cstdint>
#include <span>
#include <vector>
#include <QString>
#include "signalscope/adapters/receivers/configuration.hpp"

class QJsonObject;

namespace signalscope::desktop {
// An absent list allows offline presets. An empty list explicitly allows only auto.
std::optional<std::vector<int>> probeGainPresets(const QJsonObject& result);
QString receiverUnavailableMessage(const QJsonObject& result);
QString receiverProbeSummary(const QJsonObject& result);
std::vector<std::uint32_t> receiverMonitorRates(const QJsonObject& result,std::span<const std::uint32_t> presets);
QString receiverSettingsPrefix(const QString& backend);
QString receiverPlanSummary(const ReceiverCapabilities&,const Config&,const ReceiverPlan&);
QString receiverPlanProblemSummary(const ReceiverPlan&);
QString configurationIssueText(const ConfigurationIssue&);
QString receiverAppliedSummary(const Json&);
}
