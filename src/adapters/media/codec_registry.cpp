#include "codec_provider.hpp"
#include <mutex>
namespace signalscope::media {
namespace {
std::mutex mutex;
std::vector<CodecProvider>& registry(){static std::vector<CodecProvider> value{draCodecProvider(),ffmpegCodecProvider()};return value;}
}
std::vector<CodecProvider> codecProviders(){std::lock_guard lock(mutex);return registry();}
void registerCodecProvider(CodecProvider value) {
    if(value.id.empty() || !value.accepts || !value.create || !value.capabilities || !value.describe || !value.diagnostic)throw std::invalid_argument("media.invalid_codec_provider");
    std::lock_guard lock(mutex);
    for(const auto& known:registry())if(known.id==value.id)throw std::invalid_argument("media.duplicate_codec_provider");
    registry().push_back(std::move(value));
}
std::optional<CodecProvider> resolveCodecProvider(const EncodedTrack& track) {
    for(const auto& provider:codecProviders())if(provider.accepts(track))return provider;
    return std::nullopt;
}
Json providerDiagnostic(const TextCatalog& catalog,const std::string& key,const Json& arguments) {
    Json result={{"message_key",key},{"arguments",arguments}};catalog.resolve(result);return result;
}
}
