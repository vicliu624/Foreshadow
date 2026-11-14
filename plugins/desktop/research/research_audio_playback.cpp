#include "research_audio_playback.hpp"
#include "pcm_audio_backend.hpp"
#include <algorithm>
#include <QObject>
#include <QTimer>
#ifdef SIGNALSCOPE_HAS_AUDIO
#include "research_pcm_source.hpp"
#include <QAudioDevice>
#include <QAudioSink>
#include <QMediaDevices>
#endif

namespace signalscope::desktop {
struct ResearchAudioPlayback::Impl {
    ResearchAudioPlayback& owner;
    QObject context;
    bool active=false;
    float volume=1;
    std::uint64_t generation=0;
#ifdef SIGNALSCOPE_HAS_AUDIO
    std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<ResearchPcmSource> source;
#endif
    explicit Impl(ResearchAudioPlayback& owner):owner(owner){initializePcmAudioBackend();}
    void state(bool value) {
        if(active==value)return;
        active=value;
        if(owner.playingChanged)owner.playingChanged(value);
    }
    void stop() {
        ++generation;
#ifdef SIGNALSCOPE_HAS_AUDIO
        if(sink) {QObject::disconnect(sink.get(),nullptr,&context,nullptr);sink->reset();sink.reset();}
        source.reset();
#endif
        state(false);
    }
    void fail(const QString& key) {stop();if(owner.failed)owner.failed(key);}
    void play(const ResearchAudio& input) {
        stop();
        // Revalidate at the protocol boundary, including results constructed by
        // independent providers. Never interpret a RIFF header as PCM samples.
        ResearchAudio audio;
        try {audio=readResearchAudio(input.wav);} catch(...) {fail("invalid_audio");return;}
#ifdef SIGNALSCOPE_HAS_AUDIO
        const auto output=QMediaDevices::defaultAudioOutput();
        if(output.isNull()){fail("output_unavailable");return;}
        QAudioFormat format;format.setSampleRate(audio.rate);format.setChannelCount(audio.channels);
        format.setSampleFormat(QAudioFormat::Int16);
        if(!output.isFormatSupported(format))format=output.preferredFormat();
        if(!output.isFormatSupported(format)){fail("output_format");return;}
        try{source=std::make_unique<ResearchPcmSource>(audio,format);}catch(const std::exception&){fail("output_format");return;}
        sink=std::make_unique<QAudioSink>(output,format);sink->setVolume(volume);
        const auto current=generation;
        QObject::connect(sink.get(),&QAudioSink::stateChanged,&context,[this,current](QAudio::State){
            // Do not destroy an audio sink from its own stateChanged stack.
            QTimer::singleShot(0,&context,[this,current]{
                if(current!=generation || !active || !sink)return;
                const auto status=sink->state();
                const auto error=sink->error();
                if(error!=QAudio::NoError && error!=QAudio::UnderrunError){fail("output_failed");return;}
                if(status==QAudio::IdleState && source->atEnd())stop();
                else if(status==QAudio::StoppedState)fail("output_stopped");
            });
        });
        state(true);sink->start(source.get());
        if(sink->error()!=QAudio::NoError)fail("output_open");
#else
        fail("audio_unavailable");
#endif
    }
};
ResearchAudioPlayback::ResearchAudioPlayback():impl_(std::make_unique<Impl>(*this)){}
ResearchAudioPlayback::~ResearchAudioPlayback(){playingChanged={};failed={};impl_->stop();}
void ResearchAudioPlayback::play(const ResearchAudio& audio){impl_->play(audio);}
void ResearchAudioPlayback::stop(){impl_->stop();}
bool ResearchAudioPlayback::playing() const{return impl_->active;}
void ResearchAudioPlayback::setVolume(float volume){
    impl_->volume=std::clamp(volume,0.f,1.f);
#ifdef SIGNALSCOPE_HAS_AUDIO
    if(impl_->sink)impl_->sink->setVolume(impl_->volume);
#endif
}
}
