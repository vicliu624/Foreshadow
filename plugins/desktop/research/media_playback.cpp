#include "media_playback.hpp"
#include "pcm_audio_backend.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/transport/mpeg_ts.hpp"
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <QTimer>
#ifdef SIGNALSCOPE_HAS_AUDIO
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#endif
#include <atomic>
#include <bit>
#include <cmath>
#include <deque>
#include <mutex>

namespace signalscope::desktop {
namespace {
constexpr std::size_t inputBudget=4*1024*1024,cacheBudget=16*1024*1024,videoBudget=64*1024*1024,audioBudget=1024*1024;
constexpr std::size_t audioPrebufferBytes=48000*4*80/1000;
constexpr auto noPts=std::numeric_limits<std::int64_t>::min();
QString qtPath(const std::filesystem::path& path){return QString::fromUtf8(path_to_utf8(path));}
std::filesystem::path helperPath() {
    if(const auto path=environment_utf8("SIGNALSCOPE_MEDIA_HELPER"))return path_from_utf8(*path);
#ifdef _WIN32
    return application_directory()/"signalscope-media.exe";
#else
    return application_directory()/"signalscope-media";
#endif
}
}
class MediaPlayback::Impl {
public:
    Impl():catalog(TextCatalog::resource("media-playback")) {
        initializePcmAudioBackend();
        context=new QObject;context->moveToThread(&thread);
        QObject::connect(&thread,&QThread::finished,context,&QObject::deleteLater);thread.start();
        control([this]{timer=new QTimer(context);timer->setInterval(5);timer->setTimerType(Qt::PreciseTimer);
            QObject::connect(timer,&QTimer::timeout,context,[this]{try{tick();}catch(const std::exception& error){fail(error.what());}});
            publish();});
    }
    ~Impl(){control([this]{stop();});thread.quit();thread.wait();}
    template<class Function>void control(Function&& fn) {
        std::exception_ptr error;const auto invoke=[&]{try{fn();}catch(...){error=std::current_exception();}publish();};
        if(QThread::currentThread()==&thread)invoke();else QMetaObject::invokeMethod(context,invoke,Qt::BlockingQueuedConnection);
        if(error)std::rethrow_exception(error);
    }
    Json diagnostic(const std::string& key) const {Json value={{"message_key",key}};catalog.resolve(value);return value;}
    void stopProcess() {
        if(process){process->blockSignals(true);if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(1000);}process->deleteLater();process=nullptr;}
        parser.reset();
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(sink)sink->reset();sink.reset();device=nullptr;
#endif
        audio.clear();audioBytes=0;video.clear();videoBytes=0;wall.invalidate();audioAnchor=noPts;writtenFrames=writtenBytes=0;audioTail=false;
        lastAudioEnd=noPts;nextVideoPts=noPts;audioOpened=false;audioError=Json::object();helperEnded=false;inputClosed=false;
        std::atomic_store(&latest,std::shared_ptr<const MediaVideoFrame>{});lastMedia.invalidate();pendingAudioUnderflow=false;audioWasActive=false;
    }
    void stop(bool clearCache=false) {
        timer->stop();
        enabled=false;stopProcess();
        {std::lock_guard lock(inputMutex);input.clear();inputBytes=0;inputEnded=false;}
        current.reset();currentOffset=0;replay.clear();replayOffset=0;if(clearCache){cache.clear();cacheBytes=0;}
        state="stopped";publish();
    }
    void start(MediaSelection value,std::filesystem::path executable) {
        if(value.program>65535 || value.audio_pid< -1 || value.audio_pid>8191 || value.video_pid< -1 || value.video_pid>8191)
            throw std::invalid_argument("player.invalid_selection");
        stop(true);selection=std::move(value);helper=executable.empty()?helperPath():std::move(executable);
        videoDecoded=audioDecoded=presented=lateVideo=underruns=unavailableAudio=clockResets=sequence=0;
        syncSamples=0;maximumSyncOffset=lastVideoOffset=0;
        totalWrittenBytes=decodeErrors=unknownVideoPts=unknownAudioPts=resetVideo=resetAudio=0;decodeDiagnostic=Json::object();
        audioOverflows=eofAudioDrains=0;pendingFailure=false;underflow=false;ending=Json::object();catalogState=Json::object();deviceName.clear();
        lastDiagnostic=Json::object();capabilities=Json::object();programs=Json::array();transportBytes=0;backpressure=0;
        if(!std::filesystem::is_regular_file(helper)){fail("player.helper_missing");return;}
        enabled=true;launch();
    }
    void launch() {
        ++generation;const auto token=generation;state="buffering";helperEnded=false;inputClosed=false;
        process=new QProcess(context);process->setProgram(qtPath(helper));
        QStringList args{"--realtime","--program",QString::number(selection.program),"--text-encoding",QString::fromStdString(selection.text_encoding)};
        if(selection.audio_pid>=0)args<<"--audio-pid"<<QString::number(selection.audio_pid);
        if(selection.video_pid>=0)args<<"--video-pid"<<QString::number(selection.video_pid);
        process->setArguments(args);stderrTail.clear();
        parser=std::make_unique<media::MessageParser>([this,token](media::Message message){if(token==generation)receive(std::move(message));});
        auto* child=process;
        QObject::connect(child,&QProcess::readyReadStandardOutput,context,[this,child,token]{
            if(token!=generation || child!=process)return;
            try{readOutput();}catch(const std::exception& error){fail(error.what());}
        });
        QObject::connect(child,&QProcess::readyReadStandardError,context,[this,child,token]{
            if(token!=generation || child!=process)return;stderrTail+=child->readAllStandardError();if(stderrTail.size()>16384)stderrTail=stderrTail.last(16384);
        });
        QObject::connect(child,&QProcess::errorOccurred,context,[this,token](QProcess::ProcessError error){
            if(token==generation && error==QProcess::FailedToStart)fail("player.helper_start");
        });
        QObject::connect(child,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),context,[this,child,token](int code,QProcess::ExitStatus exit){
            if(token!=generation || child!=process)return;
            try{readOutput();parser->finish();}catch(const std::exception& error){fail(error.what());return;}
            helperEnded=true;
            if(exit!=QProcess::NormalExit || code!=0){fail("player.helper_failed");return;}
            if(pendingAudioUnderflow){++eofAudioDrains;pendingAudioUnderflow=false;}
            if(!videoDecoded && !audioDecoded){fail("player.no_frames");return;}
            state="draining";publish();
        });
        timer->start();process->start();publish();
    }
    void select(MediaSelection value) {
        if(!process)return;
        if(value.program>65535 || value.audio_pid< -1 || value.audio_pid>8191 || value.video_pid< -1 || value.video_pid>8191)
            throw std::invalid_argument("player.invalid_selection");
        stopProcess();selection=std::move(value);replay=cache;replayOffset=0;currentOffset=0;
        videoDecoded=audioDecoded=presented=lateVideo=sequence=0;syncSamples=0;maximumSyncOffset=lastVideoOffset=0;
        totalWrittenBytes=decodeErrors=unknownVideoPts=unknownAudioPts=resetVideo=resetAudio=0;decodeDiagnostic=Json::object();
        underruns=unavailableAudio=clockResets=audioOverflows=eofAudioDrains=0;
        pendingFailure=false;underflow=false;lastDiagnostic=Json::object();catalogState=Json::object();ending=Json::object();enabled=true;launch();
    }
    void fail(const std::string& key) {
        if(state=="error")return;
        const auto previous=lastDiagnostic;
        stopProcess();enabled=false;state="error";lastDiagnostic=diagnostic(key);
        timer->stop();
        if(lastDiagnostic.value("message",Json()).is_string() && lastDiagnostic.at("message")==key) {
            lastDiagnostic=diagnostic("player.protocol_error");lastDiagnostic["detail"]=key;
        }
        if(!previous.empty())lastDiagnostic["cause"]=previous;publish();
    }
    bool offer(std::span<const std::uint8_t> bytes) noexcept {
        if(!enabled || bytes.empty())return false;
        try {
            if(bytes.size()>inputBudget){++backpressure;return false;}
            auto block=std::make_shared<const Bytes>(bytes.begin(),bytes.end());
            std::unique_lock lock(inputMutex,std::try_to_lock);
            if(!lock || !enabled || inputEnded || inputBytes>inputBudget-bytes.size()){++backpressure;return false;}
            input.push_back(std::move(block));inputBytes+=bytes.size();return true;
        }catch(...){++backpressure;return false;}
    }
    void readOutput() {
        while(process && process->bytesAvailable()) {
            const auto bytes=process->read(65536);
            parser->feed({reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())});
        }
    }
    void receive(media::Message message) {
        switch(message.type) {
        case media::MessageType::capabilities:capabilities=std::move(message.metadata);break;
        case media::MessageType::catalog:
            programs=message.metadata.value("programs",Json::array());catalogState=std::move(message.metadata);break;
        case media::MessageType::status:
            if(message.metadata.contains("arguments") && message.metadata.at("arguments").contains("decode_errors")) {
                decodeErrors=std::max(decodeErrors,message.metadata.at("arguments").at("decode_errors").get<std::uint64_t>());decodeDiagnostic=message.metadata;
            }
            lastDiagnostic=std::move(message.metadata);break;
        case media::MessageType::error:lastDiagnostic=std::move(message.metadata);pendingFailure=true;break;
        case media::MessageType::ended:decodeErrors=std::max<std::uint64_t>(decodeErrors,message.metadata.value("decode_errors",0ULL));ending=std::move(message.metadata);break;
        case media::MessageType::video:receiveVideo(std::move(message));break;
        case media::MessageType::audio:receiveAudio(std::move(message));break;
        }
    }
    void receiveVideo(media::Message message) {
        lastMedia.restart();
        const auto& meta=message.metadata;const auto width=meta.at("width").get<int>(),height=meta.at("height").get<int>(),stride=meta.at("stride").get<int>();
        if(meta.value("format",std::string())!="rgb24" || width<=0 || height<=0 || width>8192 || height>8192 ||
           stride<width*3 || std::size_t(stride)*height!=message.payload.size())throw std::runtime_error("player.video_format");
        ++videoDecoded;
        const auto pts=message.pts_us;
        if(pts!=noPts && nextVideoPts!=noPts && std::abs(pts-nextVideoPts)>2000000)resetClock();
        const auto size=std::size_t((width*3+3)/4*4)*height;
        while(!video.empty() && videoBytes+size>videoBudget){videoBytes-=video.front()->image.sizeInBytes();video.pop_front();++lateVideo;}
        if(size>videoBudget)throw std::runtime_error("player.video_budget");
        QImage image(message.payload.data(),width,height,stride,QImage::Format_RGB888);auto frame=std::make_shared<MediaVideoFrame>();frame->image=image.copy();
        frame->timestamp_known=message.pts_us!=noPts;
        if(!frame->timestamp_known)++unknownVideoPts;
        const auto hint=clock();
        frame->pts_us=message.pts_us==noPts?(nextVideoPts==noPts?(hint==noPts?0:hint):nextVideoPts):message.pts_us;
        nextVideoPts=frame->pts_us+meta.value("duration_us",std::int64_t(40000));
        const auto sarNum=meta.value("sample_aspect_num",1),sarDen=meta.value("sample_aspect_den",1);
        frame->aspect_ratio=double(width)/height*(sarNum>0 && sarDen>0?double(sarNum)/sarDen:1.);
        frame->sequence=++sequence;frame->generation=generation;videoBytes+=frame->image.sizeInBytes();video.push_back(std::move(frame));
        anchorWall(video.back()->pts_us);
    }
    void receiveAudio(media::Message message) {
        lastMedia.restart();
        if(pendingAudioUnderflow){++underruns;pendingAudioUnderflow=false;}
        if(audioTail){audioAnchor=noPts;audioTail=false;}
        const auto& meta=message.metadata;const auto frames=meta.at("frames").get<std::size_t>();
        if(meta.value("format",std::string())!="s16_le" || meta.at("sample_rate")!=48000 || meta.at("channels")!=2 ||
            frames>audioBudget/4 || frames*4!=message.payload.size())throw std::runtime_error("player.audio_format");
        audioDecoded+=frames;if(message.pts_us==noPts)unknownAudioPts+=frames;anchorWall(message.pts_us);
        if(!audioOpened)openAudio();
        if(!audioAvailable()){unavailableAudio+=frames;return;}
        if(message.pts_us!=noPts && lastAudioEnd!=noPts && std::abs(message.pts_us-lastAudioEnd)>500000)resetClock();
        lastAudioEnd=message.pts_us==noPts?noPts:message.pts_us+static_cast<std::int64_t>(frames*1000000/48000);
        if(audioBytes+message.payload.size()>audioBudget){resetClock();++audioOverflows;}
        if constexpr(std::endian::native==std::endian::big)for(std::size_t i=0;i<message.payload.size();i+=2)std::swap(message.payload[i],message.payload[i+1]);
        audioBytes+=message.payload.size();audio.push_back({std::move(message.payload),0,message.pts_us});
    }
    void anchorWall(std::int64_t pts) {if(!wall.isValid() && pts!=noPts){wallPts=pts;wall.start();}}
    bool audioAvailable() const {
#ifdef SIGNALSCOPE_HAS_AUDIO
        return bool(sink);
#else
        return false;
#endif
    }
    void openAudio() {
        audioOpened=true;audioWasActive=false;
#ifdef SIGNALSCOPE_HAS_AUDIO
        auto output=QMediaDevices::defaultAudioOutput();
        if(!outputDevice.empty()) {
            output={};for(const auto& value:QMediaDevices::audioOutputs())if(value.id().toBase64().toStdString()==outputDevice){output=value;break;}
        }
        QAudioFormat format;format.setSampleRate(48000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Int16);
        if(output.isNull() || !output.isFormatSupported(format)){audioError=diagnostic("player.audio_unavailable");return;}
        sink=std::make_unique<QAudioSink>(output,format);sink->setBufferSize(48000*4/5);applyVolume();
        deviceName=output.description().toUtf8().toStdString();
#else
        audioError=diagnostic("player.audio_unavailable");
#endif
    }
    void resetClock() {
        resetVideo+=video.size();for(const auto& block:audio)resetAudio+=(block.bytes.size()-block.offset)/4;
        ++clockResets;audio.clear();audioBytes=0;video.clear();videoBytes=0;audioAnchor=noPts;writtenFrames=writtenBytes=0;wall.invalidate();lastAudioEnd=noPts;audioTail=false;
        pendingAudioUnderflow=false;underflow=false;
#ifdef SIGNALSCOPE_HAS_AUDIO
        const auto reopen=audioOpened;if(sink)sink->reset();sink.reset();device=nullptr;audioOpened=false;if(reopen)openAudio();
#endif
    }
    void applyVolume() {
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(sink)sink->setVolume(muted?0:QAudio::convertVolume(volume,QAudio::LogarithmicVolumeScale,QAudio::LinearVolumeScale));
#endif
    }
    void loseAudio() {
        // A failed device must not leave PCM queued forever at EOF. Continue
        // video against the last known playback position and report the loss.
        const auto position=clock();
        for(const auto& block:audio)unavailableAudio+=(block.bytes.size()-block.offset)/4;
        audio.clear();audioBytes=0;
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(sink)sink->reset();sink.reset();device=nullptr;
#endif
        audioError=diagnostic("player.audio_output_failed");audioAnchor=noPts;audioTail=false;
        pendingAudioUnderflow=false;underflow=false;audioWasActive=false;
        if(position!=noPts){wallPts=position;wall.start();}
    }
    void pumpAudio() {
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(!sink)return;
        if(!device) {
            if(audioBytes<audioPrebufferBytes && !helperEnded)return;
            device=sink->start();
            if(!device || (sink->error()!=QAudio::NoError && sink->error()!=QAudio::UnderrunError)){loseAudio();return;}
        }
        if(sink->error()!=QAudio::NoError && sink->error()!=QAudio::UnderrunError){loseAudio();return;}
        if(sink->state()==QAudio::ActiveState)audioWasActive=true;
        if(writtenFrames && audioWasActive && sink->state()==QAudio::IdleState && !helperEnded) {
            if(!underflow){
                // Audio can end while TS continues with video or null packets.
                // Publish the wait immediately; resumed PCM proves an underrun,
                // while normal decoder EOF proves a completed audio tail.
                // The device's initial IdleState is not a playback outage.
                pendingAudioUnderflow=true;
                underflow=true;
            }
        }else underflow=false;
        while(!audio.empty() && sink->bytesFree()>0) {
            auto& block=audio.front();
            if(audioAnchor==noPts && block.pts!=noPts){audioAnchor=block.pts;audioReference=sink->processedUSecs();}
            const auto count=std::min<qint64>(sink->bytesFree(),static_cast<qint64>(block.bytes.size()-block.offset));
            const auto accepted=device->write(reinterpret_cast<const char*>(block.bytes.data()+block.offset),count);
            if(accepted<0){loseAudio();return;}
            if(!accepted)break;
            block.offset+=static_cast<std::size_t>(accepted);audioBytes-=static_cast<std::size_t>(accepted);writtenBytes+=accepted;totalWrittenBytes+=accepted;
            writtenFrames=writtenBytes/4;
            if(block.offset==block.bytes.size())audio.pop_front();
        }
#endif
    }
    std::int64_t clock() const {
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(sink && !audioTail && audioAnchor!=noPts && writtenFrames)return audioAnchor+std::max<qint64>(0,sink->processedUSecs()-audioReference);
#endif
        return wall.isValid()?wallPts+wall.elapsed()*1000:noPts;
    }
    void pumpVideo() {
        const auto now=clock();if(now==noPts)return;
        std::shared_ptr<MediaVideoFrame> candidate;
        while(!video.empty() && video.front()->pts_us<=now+15000) {
            if(candidate)++lateVideo;
            candidate=std::move(video.front());video.pop_front();videoBytes-=candidate->image.sizeInBytes();
        }
        if(candidate){
            lastVideoOffset=candidate->pts_us-now;
            if(audioAvailable() && !audioTail && audioAnchor!=noPts && candidate->timestamp_known) {
                ++syncSamples;maximumSyncOffset=std::max(maximumSyncOffset,std::abs(lastVideoOffset));
            }
            std::atomic_store(&latest,std::shared_ptr<const MediaVideoFrame>(std::move(candidate)));++presented;
        }
    }
    void pumpInput() {
        if(!process || process->state()!=QProcess::Running || inputClosed || helperEnded)return;
        for(unsigned attempt=0;attempt<8 && process->bytesToWrite()<32768;++attempt) {
            const bool replaying=!replay.empty();
            if(!replaying && !current) {
                std::lock_guard lock(inputMutex);
                if(!input.empty()){current=std::move(input.front());input.pop_front();inputBytes-=current->size();currentOffset=0;}
                else {if(inputEnded){process->closeWriteChannel();inputClosed=true;}return;}
            }
            auto block=replaying?replay.front():current;auto& offset=replaying?replayOffset:currentOffset;
            const auto count=std::min<std::size_t>(16384,block->size()-offset);
            const auto accepted=process->write(reinterpret_cast<const char*>(block->data()+offset),static_cast<qint64>(count));
            if(accepted<0)throw std::runtime_error("player.helper_input");
            offset+=static_cast<std::size_t>(accepted);
            if(offset==block->size()) {
                if(replaying){replay.pop_front();replayOffset=0;}
                else {
                    cache.push_back(current);cacheBytes+=current->size();transportBytes+=current->size();current.reset();currentOffset=0;
                    while(cacheBytes>cacheBudget){cacheBytes-=cache.front()->size();cache.pop_front();}
                }
            }
        }
    }
    void tick() {
        if(pendingFailure){pendingFailure=false;fail("player.decoder_error");return;}
        if(process && state!="error") {
            pumpInput();pumpAudio();
#ifdef SIGNALSCOPE_HAS_AUDIO
            if(audio.empty() && sink && audioWasActive && sink->state()==QAudio::IdleState && writtenFrames && !audioTail) {
                wallPts=clock();wall.start();audioTail=true;
            }
#endif
            pumpVideo();
            if(presented || writtenFrames)state=helperEnded?"draining":lastMedia.isValid() && lastMedia.elapsed()>1000 && video.empty() && audio.empty()?"buffering":"playing";
            if(helperEnded && video.empty() && audio.empty()) {
                bool pending=false;
#ifdef SIGNALSCOPE_HAS_AUDIO
                pending=sink && sink->state()!=QAudio::IdleState;
#endif
                if(!pending){state="finished";enabled=false;timer->stop();publish();}
            }
        }
        if(!publication.isValid() || publication.elapsed()>=100){publish();publication.restart();}
    }
    void publish() {
        std::size_t queued=0;{std::lock_guard lock(inputMutex);queued=inputBytes;}
        Json result={{"state",state},{"generation",generation},{"programs",programs},{"catalog",catalogState},
            {"selection",{{"program",selection.program},{"audio_pid",selection.audio_pid},{"video_pid",selection.video_pid}}},
            {"capabilities",capabilities},{"diagnostic",lastDiagnostic},{"decode_diagnostic",decodeDiagnostic},{"decode_errors",decodeErrors},{"audio_error",audioError},{"ending",ending},
            {"input_queue_bytes",queued},{"input_budget_bytes",inputBudget},{"transport_bytes",transportBytes},
            {"input_backpressure_events",backpressure.load()},{"cache_bytes",cacheBytes},{"cache_budget_bytes",cacheBudget},
            {"video_queue_bytes",videoBytes},{"video_budget_bytes",videoBudget},{"audio_queue_bytes",audioBytes},{"audio_budget_bytes",audioBudget},
            {"audio_prebuffer_bytes",audioPrebufferBytes},
            {"decoded_video_frames",videoDecoded},{"decoded_audio_frames",audioDecoded},{"presented_video_frames",presented},
            {"written_audio_frames",totalWrittenBytes/4},{"clock_written_audio_frames",writtenFrames},{"unavailable_audio_frames",unavailableAudio},{"late_video_frames",lateVideo},
            {"video_frames_without_pts",unknownVideoPts},{"audio_frames_without_pts",unknownAudioPts},{"clock_discarded_video_frames",resetVideo},{"clock_discarded_audio_frames",resetAudio},
            {"audio_underruns",underruns},{"audio_overflows",audioOverflows},{"clock_resets",clockResets},{"clock_us",clock()},
            {"audio_eof_drains",eofAudioDrains},{"audio_output_waiting",pendingAudioUnderflow},
            {"audio_clock_video_samples",syncSamples},{"last_video_clock_offset_us",lastVideoOffset},{"maximum_audio_video_offset_us",maximumSyncOffset},
            {"counter_scope","playback_generation"},{"milliseconds_since_media",lastMedia.isValid()?lastMedia.elapsed():-1},
            {"audio_clock",audioAvailable() && !audioTail && audioAnchor!=noPts},{"volume",volume},{"muted",muted},{"output_device",deviceName},
            {"stderr",QString::fromUtf8(stderrTail).toStdString()}};
        std::atomic_store(&published,std::make_shared<const Json>(std::move(result)));
    }
    Bytes cached() {
        Bytes result;
        control([&]{
            result.reserve(cacheBytes+currentOffset);for(const auto& block:cache)result.insert(result.end(),block->begin(),block->end());
            if(current)result.insert(result.end(),current->begin(),current->begin()+static_cast<std::ptrdiff_t>(currentOffset));
        });
        return transport::completePackets(result);
    }
    struct AudioBlock {Bytes bytes;std::size_t offset;std::int64_t pts;};
    TextCatalog catalog;QThread thread;QObject* context=nullptr;QTimer* timer=nullptr;QProcess* process=nullptr;
    std::unique_ptr<media::MessageParser> parser;QElapsedTimer wall,publication,lastMedia;
#ifdef SIGNALSCOPE_HAS_AUDIO
    std::unique_ptr<QAudioSink> sink;QIODevice* device=nullptr;
#endif
    mutable std::mutex inputMutex;
    std::deque<std::shared_ptr<const Bytes>> input,cache,replay;
    std::shared_ptr<const Bytes> current;std::size_t currentOffset=0,replayOffset=0,inputBytes=0,cacheBytes=0;
    std::deque<std::shared_ptr<MediaVideoFrame>> video;std::deque<AudioBlock> audio;
    std::size_t videoBytes=0,audioBytes=0;
    std::atomic_bool enabled=false;std::atomic<std::uint64_t> backpressure=0;
    bool inputEnded=false,inputClosed=false,helperEnded=false,pendingFailure=false,audioOpened=false,underflow=false,audioTail=false,pendingAudioUnderflow=false,audioWasActive=false;
    MediaSelection selection;std::filesystem::path helper;
    std::string state="stopped",outputDevice,deviceName;double volume=.5;bool muted=false;
    Json lastDiagnostic=Json::object(),audioError=Json::object(),capabilities=Json::object(),catalogState=Json::object(),programs=Json::array(),ending=Json::object();
    QByteArray stderrTail;
    std::uint64_t generation=0,sequence=0,videoDecoded=0,audioDecoded=0,presented=0,lateVideo=0,underruns=0,
        unavailableAudio=0,audioOverflows=0,clockResets=0,writtenFrames=0,writtenBytes=0,transportBytes=0,eofAudioDrains=0;
    std::uint64_t syncSamples=0;std::int64_t maximumSyncOffset=0,lastVideoOffset=0;
    std::uint64_t totalWrittenBytes=0,decodeErrors=0,unknownVideoPts=0,unknownAudioPts=0,resetVideo=0,resetAudio=0;Json decodeDiagnostic=Json::object();
    std::int64_t wallPts=0,audioAnchor=noPts,audioReference=0,lastAudioEnd=noPts,nextVideoPts=noPts;
    // The shared_ptr atomic operations are available on all supported standard
    // libraries, including libc++ versions without atomic<shared_ptr<T>>.
    std::shared_ptr<const Json> published;
    std::shared_ptr<const MediaVideoFrame> latest;
};
MediaPlayback::MediaPlayback():impl_(std::make_unique<Impl>()){}
MediaPlayback::~MediaPlayback()=default;
void MediaPlayback::start(MediaSelection value,std::filesystem::path helper){impl_->control([&]{impl_->start(std::move(value),std::move(helper));});}
void MediaPlayback::select(MediaSelection value){impl_->control([&]{impl_->select(std::move(value));});}
bool MediaPlayback::offerTransport(std::span<const std::uint8_t> bytes) noexcept{return impl_->offer(bytes);}
void MediaPlayback::finishInput(){std::lock_guard lock(impl_->inputMutex);impl_->inputEnded=true;}
void MediaPlayback::stop(){impl_->control([&]{impl_->stop();});}
void MediaPlayback::setVolume(double value) {
    if(!std::isfinite(value) || value<0 || value>1)throw std::invalid_argument("player.volume");
    impl_->control([&]{impl_->volume=value;impl_->applyVolume();});
}
void MediaPlayback::setMuted(bool value){impl_->control([&]{impl_->muted=value;impl_->applyVolume();});}
void MediaPlayback::setOutputDevice(std::string value){impl_->control([&]{impl_->outputDevice=std::move(value);if(impl_->audioOpened)impl_->resetClock();});}
Json MediaPlayback::snapshot() const {const auto value=std::atomic_load(&impl_->published);return value?*value:Json::object();}
std::shared_ptr<const MediaVideoFrame> MediaPlayback::videoFrame() const{return std::atomic_load(&impl_->latest);}
Bytes MediaPlayback::cachedTransport() const{return impl_->cached();}
std::vector<MediaOutputDevice> MediaPlayback::outputDevices() {
    initializePcmAudioBackend();
    std::vector<MediaOutputDevice> result;
#ifdef SIGNALSCOPE_HAS_AUDIO
    const auto selected=QMediaDevices::defaultAudioOutput().id();QAudioFormat format;format.setSampleRate(48000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Int16);
    for(const auto& device:QMediaDevices::audioOutputs())result.push_back({device.id().toBase64().toStdString(),device.description().toUtf8().toStdString(),device.id()==selected,device.isFormatSupported(format)});
#endif
    return result;
}
}
