#include "signalscope/backend/dsp/recovery.hpp"
#include <algorithm>
#include <mutex>
namespace signalscope::adsb {RecoveryProvider recovery_provider();}
namespace signalscope::recovery {RecoveryProvider fsk();RecoveryProvider loraPhy();RecoveryProvider loraPaired();RecoveryProvider psk();RecoveryProvider ofdm();}
namespace signalscope {
bool supports_recovery(const RecoveryProvider& provider){return provider.protocol_version>=1 && provider.protocol_version<=researchRecoveryProtocolVersion && provider.method_version && !provider.id.empty() && !provider.resource_namespace.empty() && !provider.label_key.empty() && provider.describe && provider.recover;}
namespace {std::mutex registryMutex;std::vector<RecoveryProvider>& providers(){static std::vector<RecoveryProvider> value{recovery::fsk(),recovery::loraPhy(),recovery::loraPaired(),recovery::psk(),recovery::ofdm(),adsb::recovery_provider()};return value;}}
std::vector<RecoveryProvider> recovery_providers(){std::lock_guard lock(registryMutex);return providers();}
RecoveryProvider recovery_provider(std::string_view id){std::lock_guard lock(registryMutex);const auto found=std::find_if(providers().begin(),providers().end(),[&](const auto& item){return item.id==id;});if(found==providers().end())throw std::invalid_argument("Unknown recovery provider");return *found;}
void register_recovery(RecoveryProvider provider){if(!supports_recovery(provider))throw std::invalid_argument("Unsupported recovery provider");std::lock_guard lock(registryMutex);if(std::any_of(providers().begin(),providers().end(),[&](const auto& item){return item.id==provider.id;}))throw std::invalid_argument("Duplicate recovery provider");providers().push_back(std::move(provider));}
}
