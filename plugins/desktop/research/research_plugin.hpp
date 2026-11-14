#pragma once

#include <QString>
#include <cstdint>

namespace signalscope::desktop {
struct ResearchSampleRange {
    std::uint64_t first;
    std::uint64_t count;
};

struct ResearchPlugin {
    QString id;
    QString title;
    bool external=false;
    bool enabled=true;
    bool managed=false;
    QString toolboxId={};
};
}
