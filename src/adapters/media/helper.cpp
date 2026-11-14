#include "codec_provider.hpp"
#include "output_scheduler.hpp"
#include "signalscope/backend/transport/mpeg_ts.hpp"
#include "signalscope/backend/transport/broadcast_text.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/adler32.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <array>
#include <bit>
#include <cerrno>
#include <cstdio>
#include <chrono>
#include <thread>
#include <iostream>
#include <map>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace signalscope::media {
namespace {
std::unique_ptr<OutputScheduler> outputScheduler;
Json diagnostic(const std::string& key,const Json& arguments={}) {
    static const auto catalog=TextCatalog::resource("media-decoder");return providerDiagnostic(catalog,key,arguments);
}
void writeOutput(std::span<const std::uint8_t> bytes) {
    std::size_t first=0;
    while(first<bytes.size()) {
        const auto count=std::fwrite(bytes.data()+first,1,bytes.size()-first,stdout);
        if(!count)throw OutputFailure("media.output_closed");first+=count;
    }
    if(std::fflush(stdout))throw OutputFailure("media.output_closed");
}
void emit(Message message) {
    if(!outputScheduler){writeOutput(encode(message));return;}
    if(message.type==MessageType::ended) {
        outputScheduler->drain();message.metadata["output_scheduler"]=outputScheduler->snapshot();
    }
    if(message.type==MessageType::catalog)message.metadata["output_scheduler"]=outputScheduler->snapshot();
    if(outputScheduler->submit(std::move(message))) {
        const auto facts=outputScheduler->snapshot();
        outputScheduler->submit({MessageType::status,AV_NOPTS_VALUE,diagnostic("media.output_clock_reset",
            {{"video",facts.at("clock_discarded_video_frames")},{"audio",facts.at("clock_discarded_audio_messages")}})});
    }
}
struct Input {
    explicit Input(const std::string& encoding):catalog([encoding](auto bytes){return transport::decodeBroadcastText(bytes,encoding).value_or("");}){}
    transport::MpegTsCatalog catalog;
    std::exception_ptr failure;
    static int read(void* opaque,std::uint8_t* output,int length) {
        auto& self=*static_cast<Input*>(opaque);
        try {
            int count=0;
            do {
#ifdef _WIN32
                count=::_read(::_fileno(stdin),output,length);
#else
                count=static_cast<int>(::read(STDIN_FILENO,output,length));
#endif
            }while(count<0 && errno==EINTR);
            if(count<0)return AVERROR(errno);
            if(!count)return AVERROR_EOF;
            self.catalog.feed({output,static_cast<std::size_t>(count)});return count;
        }catch(...){self.failure=std::current_exception();return AVERROR(EIO);}
    }
};
class MediaOutput {
public:
    explicit MediaOutput(bool checksum):checksum_(checksum){}
    ~MediaOutput(){sws_freeContext(scaler_);swr_free(&resampler_);av_channel_layout_uninit(&inputLayout_);}
    void video(const AVFrame& frame,std::int64_t pts,int pid,std::int64_t durationUs) {
        if(frame.width<=0 || frame.height<=0 || frame.width>16384 || frame.height>16384)
            throw std::runtime_error("media.video_dimensions");
        const auto ratio=std::min({1.,1920./frame.width,1080./frame.height});
        const auto width=std::max(1,int(frame.width*ratio)),height=std::max(1,int(frame.height*ratio));
        const auto stride=width*3;Bytes rgb(static_cast<std::size_t>(stride)*height);
        scaler_=sws_getCachedContext(scaler_,frame.width,frame.height,static_cast<AVPixelFormat>(frame.format),
            width,height,AV_PIX_FMT_RGB24,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler_)throw std::runtime_error("media.video_conversion");
        int colorSpace=SWS_CS_DEFAULT;
        if(frame.colorspace==AVCOL_SPC_BT709)colorSpace=SWS_CS_ITU709;
        else if(frame.colorspace==AVCOL_SPC_BT2020_NCL || frame.colorspace==AVCOL_SPC_BT2020_CL)colorSpace=SWS_CS_BT2020;
        else if(frame.colorspace==AVCOL_SPC_BT470BG || frame.colorspace==AVCOL_SPC_SMPTE170M)colorSpace=SWS_CS_ITU601;
        const auto* coefficients=sws_getCoefficients(colorSpace);
        sws_setColorspaceDetails(scaler_,coefficients,frame.color_range==AVCOL_RANGE_JPEG,coefficients,1,0,1<<16,1<<16);
        std::array<std::uint8_t*,4> planes{rgb.data(),nullptr,nullptr,nullptr};std::array<int,4> strides{stride,0,0,0};
        if(sws_scale(scaler_,frame.data,frame.linesize,0,frame.height,planes.data(),strides.data())!=height)
            throw std::runtime_error("media.video_conversion");
        const auto duration=frame.duration>0?frame.duration:0;
        Json metadata={{"pid",pid},{"width",width},{"height",height},{"stride",stride},{"format","rgb24"},
            {"source_width",frame.width},{"source_height",frame.height},{"source_format",av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame.format))},
            {"sample_aspect_num",frame.sample_aspect_ratio.num},{"sample_aspect_den",frame.sample_aspect_ratio.den},
            {"corrupt",bool(frame.flags&AV_FRAME_FLAG_CORRUPT)},{"source_duration_ticks",duration},{"duration_us",durationUs},
            {"color_space",frame.colorspace},{"color_transfer",frame.color_trc},{"color_primaries",frame.color_primaries}};
        if(checksum_) {
            const auto size=av_image_get_buffer_size(static_cast<AVPixelFormat>(frame.format),frame.width,frame.height,1);
            if(size>0 && size<=int(maximumPayload)) {
                Bytes source(size);av_image_copy_to_buffer(source.data(),size,frame.data,frame.linesize,
                    static_cast<AVPixelFormat>(frame.format),frame.width,frame.height,1);
                metadata["source_adler32"]=av_adler32_update(0,source.data(),source.size());
            }
        }
        emit({MessageType::video,pts,std::move(metadata),std::move(rgb)});++videoFrames;
    }
    void audio(const AVFrame& frame,std::int64_t pts,int pid) {
        audioPid_=pid;
        if(frame.sample_rate<8000 || frame.sample_rate>384000 || frame.nb_samples<0 || frame.nb_samples>1048576 ||
           frame.ch_layout.nb_channels<1 || frame.ch_layout.nb_channels>32)throw std::runtime_error("media.audio_format");
        if(!resampler_ || rate_!=frame.sample_rate || format_!=frame.format || av_channel_layout_compare(&inputLayout_,&frame.ch_layout)) {
            swr_free(&resampler_);av_channel_layout_uninit(&inputLayout_);av_channel_layout_copy(&inputLayout_,&frame.ch_layout);
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&resampler_,&stereo,AV_SAMPLE_FMT_S16,48000,&frame.ch_layout,
                static_cast<AVSampleFormat>(frame.format),frame.sample_rate,0,nullptr)<0 || !resampler_ || swr_init(resampler_)<0)
                throw std::runtime_error("media.audio_conversion");
            rate_=frame.sample_rate;format_=frame.format;nextAudioPts_=pts;
        }
        const auto delay=swr_get_delay(resampler_,frame.sample_rate);
        const auto capacity=av_rescale_rnd(delay+frame.nb_samples,48000,frame.sample_rate,AV_ROUND_UP);
        if(capacity<=0 || capacity>262144)throw std::runtime_error("media.audio_budget");
        Bytes pcm(static_cast<std::size_t>(capacity)*4);auto* data=pcm.data();
        const auto samples=swr_convert(resampler_,&data,capacity,const_cast<const std::uint8_t**>(frame.extended_data),frame.nb_samples);
        if(samples<0)throw std::runtime_error("media.audio_conversion");
        pcm.resize(static_cast<std::size_t>(samples)*4);
        littleEndian(pcm);
        auto outputPts=pts==AV_NOPTS_VALUE?nextAudioPts_:pts-av_rescale(delay,1000000,frame.sample_rate);
        if(outputPts!=AV_NOPTS_VALUE)nextAudioPts_=outputPts+av_rescale(samples,1000000,48000);
        if(!pcm.empty())emit({MessageType::audio,outputPts,{{"pid",pid},{"sample_rate",48000},{"channels",2},
            {"format","s16_le"},{"frames",samples}},std::move(pcm)});
        audioFrames+=samples;
    }
    void finish() {
        if(!resampler_)return;
        for(unsigned attempt=0;attempt<64;++attempt) {
            Bytes pcm(4096*4);auto* data=pcm.data();
            const auto count=swr_convert(resampler_,&data,4096,nullptr,0);
            if(count<0)throw std::runtime_error("media.audio_conversion");
            if(!count)return;
            pcm.resize(static_cast<std::size_t>(count)*4);littleEndian(pcm);
            emit({MessageType::audio,nextAudioPts_,{{"pid",audioPid_},{"sample_rate",48000},{"channels",2},
                {"format","s16_le"},{"frames",count}},std::move(pcm)});
            if(nextAudioPts_!=AV_NOPTS_VALUE)nextAudioPts_+=av_rescale(count,1000000,48000);
            audioFrames+=count;
        }
        throw std::runtime_error("media.audio_drain_budget");
    }
    std::uint64_t videoFrames=0,audioFrames=0;
private:
    static void littleEndian(Bytes& bytes) {
        if constexpr(std::endian::native==std::endian::big)for(std::size_t i=0;i<bytes.size();i+=2)std::swap(bytes[i],bytes[i+1]);
    }
    bool checksum_;SwsContext* scaler_=nullptr;SwrContext* resampler_=nullptr;AVChannelLayout inputLayout_{};
    int rate_=0,format_=-1,audioPid_=-1;std::int64_t nextAudioPts_=AV_NOPTS_VALUE;
};
Json declaration(const Json& catalog,int pid) {
    for(const auto& program:catalog.at("programs"))for(const auto& stream:program.at("streams"))
        if(stream.at("pid")==pid)return stream;
    return Json::object();
}
Json capabilities() {
    Json providers=Json::array();for(const auto& provider:codecProviders())providers.push_back({{"id",provider.id},{"capabilities",provider.capabilities()}});
    return {{"protocol",protocolVersion},{"providers",std::move(providers)},{"video_format","rgb24"},
        {"audio_format","s16_le"},{"audio_rate",48000},{"audio_channels",2}};
}
class Engine {
public:
    Engine(unsigned program,int audioPid,int videoPid,bool checksum,const std::string& encoding)
        :program_(program),audioPid_(audioPid),videoPid_(videoPid),output_(checksum),input_(encoding){}
    ~Engine() {
        decoders_.clear();avformat_close_input(&format_);
        if(io_){av_freep(&io_->buffer);avio_context_free(&io_);}
    }
    void run() {
        emit({MessageType::capabilities,AV_NOPTS_VALUE,capabilities()});
        format_=avformat_alloc_context();if(!format_)throw std::bad_alloc();
        auto* buffer=static_cast<std::uint8_t*>(av_malloc(32768));if(!buffer)throw std::bad_alloc();
        io_=avio_alloc_context(buffer,32768,0,&input_,&Input::read,nullptr,nullptr);
        if(!io_){av_free(buffer);throw std::bad_alloc();}
        format_->pb=io_;format_->flags|=AVFMT_FLAG_CUSTOM_IO;format_->probesize=512*1024;format_->max_analyze_duration=250000;
        const auto opened=avformat_open_input(&format_,nullptr,av_find_input_format("mpegts"),nullptr);
        if(input_.failure)std::rethrow_exception(input_.failure);
        if(opened<0)throw std::runtime_error("media.transport_open");
        if(avformat_find_stream_info(format_,nullptr)<0 && !format_->nb_streams)throw std::runtime_error("media.no_streams");
        publishCatalog(true);
        AVPacket* packet=av_packet_alloc();if(!packet)throw std::bad_alloc();
        try {
            while(true) {
                const auto result=av_read_frame(format_,packet);
                if(input_.failure)std::rethrow_exception(input_.failure);
                if(result==AVERROR_EOF)break;
                if(result<0)throw std::runtime_error("media.transport_read");
                publishCatalog(false);
                auto* stream=format_->streams[packet->stream_index];
                if(selected(stream,packet->stream_index)) {
                    if(!decoders_.contains(packet->stream_index))openDecoder(*stream,packet->stream_index);
                    if(auto& decoder=decoders_[packet->stream_index];decoder) {
                        try{decoder->send(packet);}
                        catch(const OutputFailure&){throw;}
                        catch(const std::runtime_error& error){++decodeErrors_;emit({MessageType::status,packet->pts==AV_NOPTS_VALUE?AV_NOPTS_VALUE:av_rescale_q(packet->pts,stream->time_base,AV_TIME_BASE_Q),
                            owners_.at(packet->stream_index).diagnostic(error.what(),{{"pid",stream->id},{"decode_errors",decodeErrors_}})});}
                    }
                }
                av_packet_unref(packet);
            }
            for(auto& [index,decoder]:decoders_)if(decoder)try{decoder->send(nullptr);}catch(const OutputFailure&){throw;}catch(const std::runtime_error& error){
                ++decodeErrors_;emit({MessageType::status,AV_NOPTS_VALUE,owners_.at(index).diagnostic(error.what(),
                    {{"pid",format_->streams[index]->id},{"decode_errors",decodeErrors_}})});
            }
            output_.finish();
            input_.catalog.finish();publishCatalog(true);
            emit({MessageType::ended,AV_NOPTS_VALUE,{{"video_frames",output_.videoFrames},{"audio_frames",output_.audioFrames},{"decode_errors",decodeErrors_}}});
            av_packet_free(&packet);
        }catch(...){av_packet_free(&packet);throw;}
    }
private:
    bool selected(const AVStream* stream,unsigned index) {
        if(!program_) {
            if(format_->nb_programs)program_=format_->programs[0]->program_num;
            else return false;
        }
        bool inProgram=false;
        for(unsigned i=0;i<format_->nb_programs;++i)if(unsigned(format_->programs[i]->program_num)==program_)
            for(unsigned j=0;j<format_->programs[i]->nb_stream_indexes;++j)inProgram|=format_->programs[i]->stream_index[j]==index;
        if(!inProgram)return false;
        const auto kind=describe(*stream).value("media_type",std::string("other"));
        const auto audio=kind=="audio",video=kind=="video";
        // Unknown elementary streams remain candidates for an independent
        // codec provider. Their PMT declaration is supplied without a codec ID
        // branch in the demuxer or desktop.
        if(video){if(videoPid_==-1)videoPid_=stream->id;return videoPid_==stream->id;}
        if(audio){if(audioPid_==-1)audioPid_=stream->id;return audioPid_==stream->id;}
        return stream->id==audioPid_ || stream->id==videoPid_;
    }
    void openDecoder(AVStream& stream,unsigned index) {
        const auto raw=declaration(catalogState_,stream.id);
        EncodedTrack track{stream.codecpar,stream.time_base,raw};
        if(const auto resolved=resolveCodecProvider(track)) {
            const auto& provider=*resolved;
            owners_[index]=provider;
            try {
                decoders_[index]=provider.create(track,[this,pid=stream.id,source=&stream](const AVFrame& frame,std::int64_t pts){
                    if(frame.width>0 && frame.height>0) {
                        const auto rate=av_guess_frame_rate(format_,source,nullptr);
                        const auto duration=frame.duration>0?av_rescale_q(frame.duration,source->time_base,AV_TIME_BASE_Q):
                            rate.num>0?av_rescale(1000000,rate.den,rate.num):40000;
                        output_.video(frame,pts,pid,std::max<std::int64_t>(1,duration));
                    }else output_.audio(frame,pts,pid);
                });
                auto message=diagnostic("media.decoder_opened",{{"pid",stream.id},{"provider",provider.id}});
                message["pid"]=stream.id;message["provider"]=provider.id;emit({MessageType::status,AV_NOPTS_VALUE,std::move(message)});return;
            }catch(const std::exception& error){emit({MessageType::status,AV_NOPTS_VALUE,provider.diagnostic(error.what(),{{"pid",stream.id}})});}
        }
        decoders_[index]=nullptr;emit({MessageType::status,AV_NOPTS_VALUE,diagnostic("media.codec_unavailable",{{"pid",stream.id}})});
    }
    void publishCatalog(bool force) {
        const auto now=std::chrono::steady_clock::now();const bool statisticsDue=now-lastCatalogTime_>=std::chrono::seconds(1);
        if(!force && !statisticsDue && lastRevision_==input_.catalog.revision() && lastStreamCount_==format_->nb_streams)return;
        lastRevision_=input_.catalog.revision();lastStreamCount_=format_->nb_streams;
        catalogState_=input_.catalog.snapshot();const auto& catalog=catalogState_;Json programs=catalog.at("programs");
        for(auto& program:programs)for(auto& stream:program.at("streams")) {
            for(unsigned index=0;index<format_->nb_streams;++index)if(format_->streams[index]->id==stream.at("pid")) {
                auto& declaration=stream;const auto* parameters=format_->streams[index]->codecpar;
                declaration["codec_name"]=avcodec_get_name(parameters->codec_id);
                declaration["media_type"]=parameters->codec_type==AVMEDIA_TYPE_VIDEO?"video":parameters->codec_type==AVMEDIA_TYPE_AUDIO?"audio":"other";
                EncodedTrack track{parameters,format_->streams[index]->time_base,stream};bool supported=false;
                for(const auto& provider:codecProviders())if(provider.accepts(track)){
                    supported=true;declaration["decoder_provider"]=provider.id;declaration.update(provider.describe(track));break;
                }
                declaration["decoder_available"]=supported;
            }
        }
        if(!force && !statisticsDue && programs==lastPrograms_)return;lastPrograms_=programs;lastCatalogTime_=now;
        emit({MessageType::catalog,AV_NOPTS_VALUE,{{"programs",std::move(programs)},{"selected_program",program_},
            {"selected_audio_pid",audioPid_},{"selected_video_pid",videoPid_},{"transport",catalog}}});
    }
    Json describe(const AVStream& stream) const {
        EncodedTrack track{stream.codecpar,stream.time_base,declaration(catalogState_,stream.id)};
        if(const auto provider=resolveCodecProvider(track))return provider->describe(track);
        return {{"media_type","other"}};
    }
    unsigned program_;int audioPid_,videoPid_;MediaOutput output_;Input input_;
    AVFormatContext* format_=nullptr;AVIOContext* io_=nullptr;
    std::map<unsigned,std::unique_ptr<TrackDecoder>> decoders_;Json lastPrograms_,catalogState_;
    std::map<unsigned,CodecProvider> owners_;
    std::uint64_t lastRevision_=UINT64_MAX;unsigned lastStreamCount_=0;
    std::uint64_t decodeErrors_=0;
    std::chrono::steady_clock::time_point lastCatalogTime_{};
};
}
}
int main(int argc,char** argv) {
#ifdef _WIN32
    ::_setmode(::_fileno(stdin),_O_BINARY);::_setmode(::_fileno(stdout),_O_BINARY);
#endif
    using namespace signalscope::media;
    try {
        unsigned program=0;int audio=-1,video=-1;bool checksum=false,realtime=false;std::string encoding="ISO_6937";
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--capabilities"){emit({MessageType::capabilities,AV_NOPTS_VALUE,capabilities()});return 0;}
            if(arg=="--checksum"){checksum=true;continue;}
            if(arg=="--realtime"){realtime=true;continue;}
            if(i+1==argc)throw std::invalid_argument("media.arguments");
            if(arg=="--text-encoding") {
                encoding=argv[++i];
                if(!signalscope::transport::supportedBroadcastEncoding(encoding))throw std::invalid_argument("media.arguments");
                continue;
            }
            const auto value=std::stoul(argv[++i]);
            if(arg=="--program" && value<=65535)program=value;
            else if(arg=="--audio-pid" && value<=8191)audio=value;
            else if(arg=="--video-pid" && value<=8191)video=value;
            else throw std::invalid_argument("media.arguments");
        }
        outputScheduler=std::make_unique<OutputScheduler>(realtime,writeOutput);
        av_log_set_level(AV_LOG_ERROR);Engine(program,audio,video,checksum,encoding).run();outputScheduler->finish();return 0;
    }catch(const std::exception& error){
        if(outputScheduler){outputScheduler->cancel();outputScheduler.reset();}
        try{emit({MessageType::error,AV_NOPTS_VALUE,diagnostic(error.what())});}catch(...){}
        return 1;
    }
}
