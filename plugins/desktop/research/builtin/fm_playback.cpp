#include "plugins/desktop/research/builtin/fm_playback.hpp"
#include "plugins/desktop/research/pcm_audio_backend.hpp"
#include <cmath>
#include <utility>
#include <mutex>
#ifdef SIGNALSCOPE_HAS_AUDIO
#include <QAudioDevice>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMediaDevices>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <array>
#endif

namespace signalscope::desktop {
struct FmPlayback::Impl {
    LiveFmAudio stream;
    FmPlaybackStatus snapshot;
    mutable std::mutex snapshotMutex;
    FmPlaybackStatus published;
    std::string selectedDevice;
    std::optional<double> squelch;
    std::uint32_t inputRate=0;
    std::size_t inputStride=1;
    std::atomic<bool> failed=false;
#ifdef SIGNALSCOPE_HAS_AUDIO
    static constexpr std::size_t prebufferFrames=fm_audio_rate/10;
    QThread outputThread;
    QObject* context=nullptr;
    QTimer* timer=nullptr;
    QMediaDevices* devices=nullptr;
    QElapsedTimer gateHold;
    std::unique_ptr<QAudioSink> sink;
    QPointer<QIODevice> device;
    QByteArray pending;
    qsizetype written=0;
    std::uint64_t writtenBytes=0;
    int channels=1;
    bool primed=false,hadAudio=false,underflow=false;
    double linearVolume=1,lastLevel=-120;

    Impl() {
        initializePcmAudioBackend();
        context=new QObject;context->moveToThread(&outputThread);
        QObject::connect(&outputThread,&QThread::finished,context,&QObject::deleteLater);
        outputThread.start();
        control([this]{
        timer=new QTimer(context);devices=new QMediaDevices(context);
        timer->setInterval(5);timer->setTimerType(Qt::PreciseTimer);
        QObject::connect(timer,&QTimer::timeout,context,[this]{pump();publish();});
        QObject::connect(devices,&QMediaDevices::audioOutputsChanged,context,[this]{
            if(!sink)return;
            const auto outputs=FmPlayback::outputDevices();
            const auto desired=selectedDevice.empty()?QMediaDevices::defaultAudioOutput().id().toBase64().toStdString():selectedDevice;
            const bool available=std::any_of(outputs.begin(),outputs.end(),[&](const auto& output){return output.id==desired;});
            if(!available){fail("output_unavailable");return;}
            if(desired!=snapshot.device_id)try{closeOutput();openOutput();}catch(const FmPlaybackError& e){fail(e.key,e.what());}
            publish();
        });
        });
    }
    ~Impl(){control([this]{stop();});outputThread.quit();outputThread.wait();}
    template<class Function>void control(Function&& function) {
        std::exception_ptr error;
        const auto invoke=[&]{try{function();}catch(...){error=std::current_exception();}publish();};
        if(QThread::currentThread()==&outputThread)invoke();
        else QMetaObject::invokeMethod(context,invoke,Qt::BlockingQueuedConnection);
        if(error)std::rethrow_exception(error);
    }
    void closeOutput() {
        device=nullptr;if(sink)sink->reset();sink.reset();pending.clear();written=0;
        primed=false;underflow=false;hadAudio=false;
    }
    void openOutput() {
        auto output=QMediaDevices::defaultAudioOutput();
        if(!selectedDevice.empty()) {
            output={};
            for(const auto& candidate:QMediaDevices::audioOutputs())
                if(candidate.id().toBase64().toStdString()==selectedDevice){output=candidate;break;}
        }
        if(output.isNull())throw FmPlaybackError("output_unavailable");
        QAudioFormat format;format.setSampleRate(fm_audio_rate);format.setSampleFormat(QAudioFormat::Int16);format.setChannelCount(1);
        if(!output.isFormatSupported(format))format.setChannelCount(2);
        if(!output.isFormatSupported(format))throw FmPlaybackError("output_format");
        channels=format.channelCount();writtenBytes=0;
        sink=std::make_unique<QAudioSink>(output,format);sink->setBufferSize(fm_audio_rate*channels*2/5);
        snapshot.device_name=output.description().toUtf8().toStdString();
        snapshot.device_id=output.id().toBase64().toStdString();
        sink->setVolume(0);device=sink->start();
        if(!device || sink->error()!=QAudio::NoError)throw FmPlaybackError("output_open",std::to_string(int(sink->error())));
        gateHold.start();snapshot.state="buffering";
    }
    void volume() {
        linearVolume=QAudio::convertVolume(snapshot.volume,QAudio::LogarithmicVolumeScale,QAudio::LinearVolumeScale);
        if(sink)sink->setVolume(snapshot.muted || snapshot.squelched?0:linearVolume);
    }
    void fail(std::string key,std::string detail={}) {
        stop();snapshot.state="failed";snapshot.error_key=std::move(key);snapshot.error_detail=std::move(detail);failed=true;publish();
    }
    void pump() {
        if(!device){if(sink)fail("output_unavailable");return;}
        const auto input=stream.status();
        if(input.failed){fail("decoder_failed",input.error_detail);return;}
        const auto error=sink->error();
        if(error!=QAudio::NoError && error!=QAudio::UnderrunError){fail("output_failed",std::to_string(int(error)));return;}
        const bool starved=(primed || hadAudio) && (error==QAudio::UnderrunError || sink->state()==QAudio::IdleState);
        if(starved) {
            if(!underflow)++snapshot.underruns;
            underflow=true;primed=false;snapshot.state="recovering";
        }
        if(!primed && input.pcm_frames+std::size_t(std::max<qsizetype>(0,pending.size()-written))/(channels*2)<prebufferFrames)return;
        primed=true;
        if(!squelch)snapshot.squelched=false;
        else if(input.signal.snr_db && *input.signal.snr_db>=*squelch) {snapshot.squelched=false;gateHold.restart();}
        else if(snapshot.squelched || !input.signal.snr_db || (*input.signal.snr_db<*squelch-3 && gateHold.elapsed()>300))snapshot.squelched=true;
        volume();
        // Fill the available device budget in one visit. A single short PCM
        // write per timer interval can run dry before the next GUI wakeup.
        for(unsigned batch=0;batch<8;++batch) {
            if(pending.isEmpty()) {
                std::array<std::int16_t,1920> frames{};
                const auto capacity=std::clamp<qsizetype>(sink->bytesFree()/(channels*2),0,960);
                const auto count=stream.read(std::span(frames).first(capacity));
                if(!count)break;
                double power=0;for(std::size_t i=0;i<count;++i)power+=double(frames[i])*frames[i];
                lastLevel=10*std::log10(std::max(power/count/(32768.*32768.),1e-12));
                if(channels==2)for(auto i=count;i>0;--i)frames[2*i-1]=frames[2*i-2]=frames[i-1];
                pending=QByteArray(reinterpret_cast<const char*>(frames.data()),count*channels*2);written=0;
            }
            const auto amount=device->write(pending.constData()+written,pending.size()-written);
            if(amount<0){fail("output_write",device->errorString().toUtf8().toStdString());return;}
            written+=amount;
            snapshot.written_frames+=(writtenBytes+amount)/(channels*2)-writtenBytes/(channels*2);
            writtenBytes+=amount;
            if(written==pending.size())pending.clear();
            if(!amount)break;
        }
        if(sink->state()==QAudio::ActiveState){hadAudio=true;underflow=false;snapshot.state="playing";}
        snapshot.output_level_dbfs=snapshot.muted || snapshot.squelched || linearVolume<=0?-120:lastLevel+20*std::log10(linearVolume);
    }
#endif
    void stop() {
        failed=false;
#ifdef SIGNALSCOPE_HAS_AUDIO
        timer->stop();closeOutput();
#endif
        stream.stop();snapshot.state="stopped";snapshot.squelched=false;snapshot.output_level_dbfs=-120;
    }
    void publish() {
        auto value=snapshot;value.input=stream.status();
        value.queued_seconds=inputRate?double(value.input.queue_bytes)/(inputStride*double(inputRate)):0;
        value.queued_seconds+=double(value.input.pcm_frames)/fm_audio_rate;
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(sink)value.queued_seconds+=double(std::max<qsizetype>(0,sink->bufferSize()-sink->bytesFree()+pending.size()-written))/(channels*2*fm_audio_rate);
#endif
        std::lock_guard lock(snapshotMutex);published=std::move(value);
    }
#ifndef SIGNALSCOPE_HAS_AUDIO
    template<class Function>void control(Function&& function){function();publish();}
#endif
};

FmPlayback::FmPlayback():impl_(std::make_unique<Impl>()){}
FmPlayback::~FmPlayback()=default;
bool FmPlayback::supported() {
#ifdef SIGNALSCOPE_HAS_AUDIO
    return true;
#else
    return false;
#endif
}
std::vector<AudioOutputDevice> FmPlayback::outputDevices() {
    initializePcmAudioBackend();
    std::vector<AudioOutputDevice> result;
#ifdef SIGNALSCOPE_HAS_AUDIO
    const auto current=QMediaDevices::defaultAudioOutput().id();
    for(const auto& device:QMediaDevices::audioOutputs()) {
        QAudioFormat format;format.setSampleRate(fm_audio_rate);format.setSampleFormat(QAudioFormat::Int16);format.setChannelCount(1);
        bool supported=device.isFormatSupported(format);format.setChannelCount(2);supported=supported || device.isFormatSupported(format);
        result.push_back({device.id().toBase64().toStdString(),device.description().toUtf8().toStdString(),device.id()==current,supported});
    }
#endif
    return result;
}
void FmPlayback::start(const FmAudioConfig& config,std::size_t maximumBlockBytes,const std::string& outputDevice) {
    impl_->control([&]{
    impl_->stop();impl_->snapshot.error_key.clear();impl_->snapshot.error_detail.clear();impl_->snapshot.underruns=0;impl_->snapshot.written_frames=0;
    impl_->selectedDevice=outputDevice;impl_->inputRate=config.input_rate;impl_->inputStride=bytes_per_pair(config.format);
#ifdef SIGNALSCOPE_HAS_AUDIO
    try {
        impl_->stream.start(config,maximumBlockBytes);impl_->writtenBytes=0;impl_->openOutput();impl_->timer->start();
    }catch(const FmPlaybackError& e){impl_->fail(e.key,e.what());throw;}
    catch(const std::exception& e){impl_->fail("decoder_failed",e.what());throw;}
#else
    (void)maximumBlockBytes;throw FmPlaybackError("audio_unavailable");
#endif
    });
}
void FmPlayback::setOutputDevice(const std::string& device) {
    impl_->control([&]{
    if(device==impl_->selectedDevice)return;
    impl_->selectedDevice=device;
#ifdef SIGNALSCOPE_HAS_AUDIO
    if(impl_->sink)try {impl_->closeOutput();impl_->openOutput();}
    catch(const FmPlaybackError& e){impl_->fail(e.key,e.what());throw;}
#endif
    });
}
void FmPlayback::setVolume(double volume) {
    if(!std::isfinite(volume) || volume<0 || volume>1)throw std::invalid_argument("Audio volume must be in [0,1]");
    impl_->control([&]{impl_->snapshot.volume=volume;
#ifdef SIGNALSCOPE_HAS_AUDIO
    impl_->volume();
#endif
    });
}
void FmPlayback::setMuted(bool value) {impl_->control([&]{impl_->snapshot.muted=value;
#ifdef SIGNALSCOPE_HAS_AUDIO
    impl_->volume();
#endif
    });
}
void FmPlayback::setSquelch(std::optional<double> threshold) {
    if(threshold && (!std::isfinite(*threshold) || *threshold< -20 || *threshold>60))throw std::invalid_argument("Invalid FM squelch threshold");
    impl_->control([&]{impl_->squelch=threshold;impl_->snapshot.squelched=threshold.has_value();});
}
FmPlaybackStatus FmPlayback::status() const {
    std::lock_guard lock(impl_->snapshotMutex);return impl_->published;
}
void FmPlayback::stop(){impl_->control([&]{impl_->stop();});}
bool FmPlayback::takeFailure(){return impl_->failed.exchange(false);}
void FmPlayback::offer(std::span<const std::uint8_t> bytes,std::uint64_t first) noexcept {impl_->stream.offer(bytes,first);}
}
