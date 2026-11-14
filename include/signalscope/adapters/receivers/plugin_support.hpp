#pragma once
#include "signalscope/adapters/receivers/receiver.hpp"
#include "signalscope/adapters/receivers/plugin_api.h"

namespace signalscope::receiver_plugin {
// Optional C++ SDK. The public binary contract itself is plugin_api.h.
Config decodeConfig(const Json&);
Json encodeConfig(const Config&);
ReceiverPlan validateHardware(const ReceiverCapabilities&,const Config&);
Json describeParameters(const ReceiverCapabilities&,const Config&);
std::vector<Config> recommendHardware(const ReceiverCapabilities&,const Config&);
int request(const ReceiverBackend&,const char*,const char*,void*,AmcReceiverResult) noexcept;
int stream(const ReceiverBackend&,const char*,void*,AmcReceiverSamples,AmcReceiverReady,
           AmcReceiverCancelled,AmcReceiverResult) noexcept;
}
