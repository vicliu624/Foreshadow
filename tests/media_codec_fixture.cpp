#include "src/adapters/media/codec_provider.hpp"
extern "C" {
#include <libavutil/channel_layout.h>
}
namespace signalscope::media {
namespace {
class FixtureDecoder:public TrackDecoder {
public:
    explicit FixtureDecoder(DecodedFrameSink sink):sink_(std::move(sink)){}
    void send(const AVPacket* packet) override {
        if(!packet)return;
        AVFrame* frame=av_frame_alloc();if(!frame)throw std::bad_alloc();
        frame->format=AV_SAMPLE_FMT_S16;frame->sample_rate=48000;frame->nb_samples=16;
        av_channel_layout_default(&frame->ch_layout,2);
        if(av_frame_get_buffer(frame,0)<0){av_frame_free(&frame);throw std::bad_alloc();}
        std::fill_n(frame->data[0],64,0x34);sink_(*frame,987654);av_frame_free(&frame);
    }
private:DecodedFrameSink sink_;
};
}
CodecProvider mediaFixtureProvider() {
    return {"independent-test-codec",[](const EncodedTrack& track){return track.declaration.value("registration",std::string())=="FUTR";},
        [](const EncodedTrack&,DecodedFrameSink sink){return std::make_unique<FixtureDecoder>(std::move(sink));},
        []{return Json{{"decoders",Json::array({{{"id","future-audio"},{"media_type","audio"}}})}};},
        [](const EncodedTrack&){return Json{{"codec_name","future-audio"},{"media_type","audio"}};},
        [](const std::string& key,const Json& arguments){
            static const auto catalog=TextCatalog::resource("media-codec-fixture");return providerDiagnostic(catalog,key,arguments);
        }};
}
}
