#include "live_provider.hpp"
#include "builtin/fm_live.hpp"
#include "waveform/laboratory.hpp"
#ifdef SIGNALSCOPE_HAS_TELEVISION
#include "builtin/dtmb_tv.hpp"
#endif
#include <mutex>
namespace signalscope::desktop {
namespace {
std::mutex registryMutex;
std::vector<LiveResearchProvider>& registry(){static std::vector<LiveResearchProvider> value{fmLiveResearchProvider(),waveformLaboratoryProvider()
#ifdef SIGNALSCOPE_HAS_TELEVISION
    ,dtmbTvResearchProvider()
#endif
};return value;}
}
std::vector<LiveResearchProvider> liveResearchProviders(){std::lock_guard lock(registryMutex);return registry();}
void registerLiveResearchProvider(LiveResearchProvider provider) {
    if(provider.id.isEmpty() || !provider.create || !supportsLiveResearchProtocol(provider.protocolVersion))
        throw std::invalid_argument("Unsupported or incomplete live research provider");
    std::lock_guard lock(registryMutex);
    for(const auto& known:registry())if(known.id==provider.id)throw std::invalid_argument("Duplicate live research provider");
    registry().push_back(std::move(provider));
}
}
