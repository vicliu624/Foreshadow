#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace signalscope::desktop {
struct ResearchAudio {
    QByteArray wav;
    std::uint32_t rate;
    std::uint16_t channels;
    std::uint32_t frames;
    QString description{};
};

ResearchAudio readResearchAudio(QByteArray wav);
}
