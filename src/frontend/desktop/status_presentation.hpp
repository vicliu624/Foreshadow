#pragma once

#include "signalscope/backend/iq/iq.hpp"
#include <QLocale>
#include <QString>
#include <QTimeZone>
#include <string_view>

namespace signalscope::desktop {
struct RecordingStatus {
    QString label;
    QString code;
};

struct NarrowbandStatus {
    QString suffix;
    QString detail;
    bool degraded=false;
};

QString sessionStatus(std::string_view code);
RecordingStatus recordingStatus(const Json& metadata);
QString recordingTimeTooltip(const Json& metadata,const QLocale& locale,
                            const QTimeZone& zone);
NarrowbandStatus narrowbandStatus(const Json& status);
QString receiverReadSummary(const Json& status);
}
