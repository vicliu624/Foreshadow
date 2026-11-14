#pragma once
#include "signalscope/backend/media/protocol.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <memory>
#include <optional>

namespace signalscope::media {
// Private media-engine extension protocol. No FFmpeg type crosses the helper
// IPC or enters desktop/session consumers. Additional codecs register here.
struct EncodedTrack {
    const AVCodecParameters* parameters;
    AVRational time_base;
    Json declaration=Json::object();
};
using DecodedFrameSink=std::function<void(const AVFrame&,std::int64_t)>;
class TrackDecoder {
public:
    virtual ~TrackDecoder()=default;
    virtual void send(const AVPacket*)=0;
};
struct CodecProvider {
    std::string id;
    std::function<bool(const EncodedTrack&)> accepts;
    std::function<std::unique_ptr<TrackDecoder>(const EncodedTrack&,DecodedFrameSink)> create;
    std::function<Json()> capabilities;
    std::function<Json(const EncodedTrack&)> describe;
    std::function<Json(const std::string&,const Json&)> diagnostic;
};
Json providerDiagnostic(const TextCatalog&,const std::string& key,const Json& arguments={});
std::vector<CodecProvider> codecProviders();
void registerCodecProvider(CodecProvider);
std::optional<CodecProvider> resolveCodecProvider(const EncodedTrack&);
CodecProvider ffmpegCodecProvider();
CodecProvider draCodecProvider();
}
