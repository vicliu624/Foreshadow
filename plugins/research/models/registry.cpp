#include "signalscope/backend/dsp/reference_model.hpp"
#include "common.hpp"
#include <algorithm>
#include <mutex>

namespace signalscope {
bool supports_reference_model(const ReferenceModelProvider& provider) {
    return !provider.id.empty() && !provider.resource_namespace.empty() && !provider.label_key.empty() && provider.method_version && provider.prepare &&
        ((provider.protocol_version==1 && provider.parameters) || ((provider.protocol_version==2 || provider.protocol_version==3) && provider.describe));
}
Json reference_model_parameters(const ReferenceModelProvider& provider,const ReferenceModelContext& context,const Json& pending) {
    if(!supports_reference_model(provider) || !pending.is_object())throw std::invalid_argument("Invalid reference parameter protocol");
    const auto result=provider.protocol_version==1?provider.parameters(context):provider.describe(context,pending);
    if(!result.is_array())throw std::invalid_argument("Reference parameters must be an array");
    return result;
}
namespace {
std::mutex registry_mutex;
std::vector<ReferenceModelProvider>& providers(){static std::vector<ReferenceModelProvider> value{models::tone(),models::am(),models::fm(),models::fsk(),models::chirp(),models::lora(),models::psk(),models::ofdm()};return value;}
}
std::vector<ReferenceModelProvider> reference_model_providers(){std::lock_guard lock(registry_mutex);return providers();}
ReferenceModelProvider reference_model_provider(std::string_view id) {
    std::lock_guard lock(registry_mutex);
    const auto found=std::find_if(providers().begin(),providers().end(),[&](const auto& value){return value.id==id;});
    if(found==providers().end())throw std::invalid_argument("Unknown reference model provider");
    return *found;
}
void register_reference_model(ReferenceModelProvider provider) {
    if(!supports_reference_model(provider))throw std::invalid_argument("Invalid reference model provider");
    std::lock_guard lock(registry_mutex);
    if(std::any_of(providers().begin(),providers().end(),[&](const auto& value){return value.id==provider.id;}))throw std::invalid_argument("Duplicate reference model provider");
    providers().push_back(std::move(provider));
}
}
