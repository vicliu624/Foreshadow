#pragma once

#include "plugins/desktop/research/research_plugin.hpp"
#include <QImage>
#include <QString>
#include <stop_token>
#include <optional>

namespace signalscope::desktop {
QImage renderIqOverview(const QString& metadata,std::stop_token stop,
                        std::optional<ResearchSampleRange> range={});
}
