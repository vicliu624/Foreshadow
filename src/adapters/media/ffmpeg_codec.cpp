#include "codec_provider.hpp"
extern "C" {
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

namespace signalscope::media {
namespace {
class FfmpegDecoder final:public TrackDecoder {
public:
    FfmpegDecoder(const EncodedTrack& track,DecodedFrameSink sink):sink_(std::move(sink)),timeBase_(track.time_base) {
        const auto* codec=avcodec_find_decoder(track.parameters->codec_id);
        if(!codec)throw std::runtime_error("media.codec_unavailable");
        context_=avcodec_alloc_context3(codec);frame_=av_frame_alloc();
        if(!context_ || !frame_){cleanup();throw std::bad_alloc();}
        if(avcodec_parameters_to_context(context_,track.parameters)<0){cleanup();throw std::runtime_error("media.codec_parameters");}
        context_->thread_count=4;context_->pkt_timebase=timeBase_;context_->max_pixels=8192LL*4320;
        if(avcodec_open2(context_,codec,nullptr)<0){cleanup();throw std::runtime_error("media.codec_open");}
    }
    ~FfmpegDecoder() override {cleanup();}
    void send(const AVPacket* packet) override {
        auto result=avcodec_send_packet(context_,packet);
        if(result==AVERROR(EAGAIN)){receive();result=avcodec_send_packet(context_,packet);}
        if(result<0 && result!=AVERROR_EOF)throw std::runtime_error("media.decode_packet");
        receive();
    }
private:
    void receive() {
        while(true) {
            const auto result=avcodec_receive_frame(context_,frame_);
            if(result==AVERROR(EAGAIN) || result==AVERROR_EOF)return;
            if(result<0)throw std::runtime_error("media.decode_frame");
            const auto pts=frame_->best_effort_timestamp==AV_NOPTS_VALUE?AV_NOPTS_VALUE:
                av_rescale_q(frame_->best_effort_timestamp,timeBase_,AV_TIME_BASE_Q);
            try{sink_(*frame_,pts);}catch(...){av_frame_unref(frame_);throw;}
            av_frame_unref(frame_);
        }
    }
    void cleanup(){av_frame_free(&frame_);avcodec_free_context(&context_);}
    AVCodecContext* context_=nullptr;AVFrame* frame_=nullptr;
    DecodedFrameSink sink_;AVRational timeBase_;
};
}
CodecProvider ffmpegCodecProvider() {
    return {"ffmpeg",[](const EncodedTrack& track){return avcodec_find_decoder(track.parameters->codec_id)!=nullptr;},
        [](const EncodedTrack& track,DecodedFrameSink sink){return std::make_unique<FfmpegDecoder>(track,std::move(sink));},
        []{
            Json decoders=Json::array();void* position=nullptr;
            while(const auto* codec=av_codec_iterate(&position))if(av_codec_is_decoder(codec))
                decoders.push_back({{"id",codec->name},{"media_type",codec->type==AVMEDIA_TYPE_VIDEO?"video":codec->type==AVMEDIA_TYPE_AUDIO?"audio":"other"}});
            return Json{{"version",avcodec_version()},{"license",avcodec_license()},{"decoders",std::move(decoders)}};
        },[](const EncodedTrack& track){
            const auto type=track.parameters->codec_type;
            return Json{{"codec_name",avcodec_get_name(track.parameters->codec_id)},
                {"media_type",type==AVMEDIA_TYPE_AUDIO?"audio":type==AVMEDIA_TYPE_VIDEO?"video":"other"}};
        },[](const std::string& key,const Json& arguments){
            static const auto catalog=TextCatalog::resource("media-ffmpeg");return providerDiagnostic(catalog,key,arguments);
        }};
}
}
