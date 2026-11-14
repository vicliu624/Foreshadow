#pragma once

#include "plugins/desktop/research/research_plugin.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <optional>

namespace signalscope::desktop {
std::optional<ResearchSampleRange> researchRangeFromSeconds(const QString& start,const QString& duration,
                                                           std::uint32_t rate);
std::optional<ResearchSampleRange> replayEventRange(const Json& event,std::uint32_t rate,std::uint64_t pairs);
}
