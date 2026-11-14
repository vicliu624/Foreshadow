#pragma once
#include "plugins/desktop/research/research_plugin.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <QImage>
#include <QString>
#include <stop_token>
#include <optional>
class QWidget;

namespace signalscope::desktop {
struct ResearchControls;
ResearchControls loRaStudyControls(::QWidget* parent);
QImage renderLoRaStudy(const QString& metadata,double threshold,std::stop_token stop,
                       std::optional<ResearchSampleRange> range={},const Json& parameters=Json::object());
}
