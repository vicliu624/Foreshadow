#pragma once

#include "signalscope/adapters/modulation/detection_selection.hpp"
#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"
#include "signalscope/backend/sessions/iq_subscribers.hpp"

namespace signalscope {
struct ReplaySessionOptions {
    LoRaConfig lora;
    bool enableLora=true;
    NarrowbandSelection narrowband;
    double speed=1;
};

struct ReplaySessionObserver {
    std::function<void(const Json&)> status;
    std::function<void(const SpectrumFrame&)> spectrum;
    IqSubscribers analysis_iq;
};

// Runs synchronously on its caller's worker thread. Observer callbacks run there too.
Json replaySession(const ReplayInfo& info,ReplaySessionOptions options,
                   const std::function<bool()>& paused,const std::function<bool()>& cancelled,
                   const ReplaySessionObserver& observer={});
}
