#pragma once

#include "plugins/desktop/research/research_audio.hpp"
#include "plugins/desktop/research/research_plugin.hpp"
#include <optional>
#include <stop_token>

namespace signalscope::desktop {
struct FmResearchOptions {
    std::optional<double> frequency;
    double deemphasisUs=50;
    double deviationHz=75000;
};

ResearchAudio renderFmResearch(const QString& metadata,std::stop_token stop,
                               std::optional<ResearchSampleRange> range={},FmResearchOptions options={});
}
