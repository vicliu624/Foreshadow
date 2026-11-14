#pragma once
#include "signalscope/adapters/receivers/receiver.hpp"
#include "signalscope/adapters/modulation/lora.hpp"

namespace signalscope {
// Shared by desktop preview, CLI validation and live session startup.
ReceiverPlan planMonitorConfiguration(const ReceiverCapabilities&,const Config&,const LoRaConfig&,
                                      bool lora,bool narrowband,bool snapshots=false);
void requireMonitorConfiguration(const ReceiverPlan&);
struct MonitorRecommendation {
    Config receiver;
    double search_width_hz=0;
};
// Keep the requested frequency and detector selection. Return only combinations
// that pass the same preflight used by a live session.
std::optional<MonitorRecommendation> recommendMonitorConfiguration(
    const ReceiverCapabilities&,const Config&,const LoRaConfig&,
    bool lora,bool narrowband,bool snapshots=false);
}
