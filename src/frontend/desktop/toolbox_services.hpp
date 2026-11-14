#pragma once
#include "signalscope/backend/distribution/distribution.hpp"
#include <QString>
#include <QStringList>
namespace signalscope::desktop {
QString toolboxText(const std::string &key, const nlohmann::json &arguments = nlohmann::json::object());
QString toolboxError(const std::exception &);
std::shared_ptr<distribution::Service> makeDistributionService();
bool scheduleDistributionRestart(distribution::Service &, const QStringList &arguments);
void confirmDistributionStartup(const QStringList &arguments);
} // namespace signalscope::desktop
