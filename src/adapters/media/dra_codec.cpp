#include "codec_provider.hpp"
#include "dradec.h"
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}
#include <deque>

namespace signalscope::media {
namespace {
class DraDecoder final:public TrackDecoder {
public:
    DraDecoder(const EncodedTrack& track,DecodedFrameSink sink):timeBase_(track.time_base),sink_(std::move(sink)) {
        decoder_=DRADecCreate();if(!decoder_)throw std::bad_alloc();
    }
    ~DraDecoder() override {DRADecDestroy(decoder_);}
    void send(const AVPacket* packet) override {
        if(!packet){if(!pending_.empty())throw std::runtime_error("media.dra_truncated_frame");return;}
        if(packet->size==0)return;
        if(packet->size<0 || !packet->data || packet->size>1024*1024 || pending_.size()+packet->size>2*1024*1024)
            throw std::runtime_error("media.dra_input_budget");
        if(packet->pts!=AV_NOPTS_VALUE)anchors_.push_back({received_,av_rescale_q(packet->pts,timeBase_,AV_TIME_BASE_Q)});
        received_+=packet->size;pending_.insert(pending_.end(),packet->data,packet->data+packet->size);
        std::size_t first=0;
        while(pending_.size()-first>=6) {
            if(pending_[first]!=0x7f || pending_[first+1]!=0xff){++first;continue;}
            unsigned bit=16;
            const auto bits=[&](unsigned count){unsigned value=0;while(count--){value=(value<<1)|((pending_[first+bit/8]>>(7-bit%8))&1);++bit;}return value;};
            const auto extended=bits(1);
            const auto length=bits(extended?13:10)*4;
            const auto blocks=1u<<bits(2);
            const auto rate=bits(4);
            const auto normal=bits(extended?6:3)+1;
            const auto lfe=bits(extended?2:1);
            const auto channels=normal+lfe;
            const auto aux=bits(1);
            if(length<8 || length>32764 || rate>12 || channels>8 || blocks!=8){++first;continue;}
            if(pending_.size()-first<length)break;
            if(aux)throw std::runtime_error("media.dra_auxiliary_unsupported");
            const auto position=consumed_+first;
            while(!anchors_.empty() && anchors_.front().first<=position){nextPts_=anchors_.front().second;anchors_.pop_front();}
            if(anchors_.size()>1024)throw std::runtime_error("media.dra_timestamp_budget");
            if(DRADecSendData(decoder_,pending_.data()+first,static_cast<int>(length))<0)throw std::runtime_error("media.dra_input");
            unsigned char* pcm=nullptr;int size=0;
            if(DRADecRecvFrame(decoder_,&pcm,&size)<0 || !pcm || size<=0){first+=length;continue;}
            DRAFrameInfo info{};
            if(DRADecGetFrameInfo(decoder_,&info)<0 || info.nChannels<1 || info.nChannels>8 || info.nSampleRate<8000 ||
               info.nSampleRate>192000 || size!=info.nChannels*1024*2)throw std::runtime_error("media.dra_output");
            AVFrame* frame=av_frame_alloc();if(!frame)throw std::bad_alloc();
            frame->format=AV_SAMPLE_FMT_S16;frame->sample_rate=info.nSampleRate;frame->nb_samples=1024;
            av_channel_layout_default(&frame->ch_layout,info.nChannels);
            if(av_frame_get_buffer(frame,0)<0){av_frame_free(&frame);throw std::bad_alloc();}
            std::copy_n(pcm,size,frame->data[0]);
            try{sink_(*frame,nextPts_);}catch(...){av_frame_free(&frame);throw;}
            av_frame_free(&frame);
            if(nextPts_!=AV_NOPTS_VALUE)nextPts_+=av_rescale(1024,1000000,info.nSampleRate);
            first+=length;
        }
        if(first){pending_.erase(pending_.begin(),pending_.begin()+static_cast<std::ptrdiff_t>(first));consumed_+=first;}
    }
private:
    void* decoder_=nullptr;AVRational timeBase_;DecodedFrameSink sink_;Bytes pending_;
    std::deque<std::pair<std::uint64_t,std::int64_t>> anchors_;
    std::uint64_t received_=0,consumed_=0;std::int64_t nextPts_=AV_NOPTS_VALUE;
};
bool accepts(const EncodedTrack& track) {
    if(track.parameters->codec_tag==MKTAG('D','R','A','1'))return true;
    const auto& declaration=track.declaration;
    if(declaration.value("stream_type",0)==0xd1)return true;
    for(const auto& item:declaration.value("descriptors",Json::array()))
        if(item.value("tag",0)==5 && item.value("data",Bytes{})==Bytes{'D','R','A','1'})return true;
    return false;
}
}
CodecProvider draCodecProvider() {
    return {"dra",accepts,[](const EncodedTrack& track,DecodedFrameSink sink){return std::make_unique<DraDecoder>(track,std::move(sink));},
        []{return Json{{"decoders",Json::array({{{"id","dra"},{"media_type","audio"},{"maximum_channels",8},{"auxiliary_profile",false}}})}};},
        [](const EncodedTrack&){return Json{{"codec_name","dra"},{"media_type","audio"}};},
        [](const std::string& key,const Json& arguments){
            static const auto catalog=TextCatalog::resource("media-dra");return providerDiagnostic(catalog,key,arguments);
        }};
}
}
