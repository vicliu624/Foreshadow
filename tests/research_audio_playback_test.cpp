#include "plugins/desktop/research/research_audio_playback.hpp"
#include <QApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QIODevice>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <array>
#include <limits>
#include <cstring>
#include <cmath>
#ifdef AMC_TEST_AUDIO_OUTPUT
#include "plugins/desktop/research/research_pcm_source.hpp"
#include <QAudioDevice>
#include <QMediaDevices>
#include <QtEndian>
#include <QFile>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(Q_OS_MACOS)
#include <mach-o/dyld.h>
#endif
#endif
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
ResearchAudio fixture(unsigned rate=48000,unsigned channels=1,unsigned frames=24000) {
    QByteArray wav;QDataStream out(&wav,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF",4);out<<quint32(36+frames*channels*2);out.writeRawData("WAVEfmt ",8);
    out<<quint32(16)<<quint16(1)<<quint16(channels)<<quint32(rate)<<quint32(rate*channels*2)
       <<quint16(channels*2)<<quint16(16);out.writeRawData("data",4);out<<quint32(frames*channels*2);
    // Independent finite PCM input; silent playback is intentional for CI.
    for(unsigned i=0;i<frames*channels;++i)out<<qint16(0);
    return readResearchAudio(wav);
}
template<class Predicate>bool wait(Predicate predicate,int milliseconds) {
    QElapsedTimer timer;timer.start();
    while(!predicate() && timer.elapsed()<milliseconds){QApplication::processEvents();QThread::msleep(5);}
    return predicate();
}
#ifdef AMC_TEST_AUDIO_OUTPUT
void noCodecBackend() {
#if QT_VERSION >= QT_VERSION_CHECK(6,8,0)
    QStringList images;
#if defined(Q_OS_LINUX)
    QFile maps("/proc/self/maps");require(maps.open(QIODevice::ReadOnly),"Could not inspect PCM process images");
    images=QString::fromUtf8(maps.readAll()).split('\n');
#elif defined(Q_OS_WIN)
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    require(snapshot!=INVALID_HANDLE_VALUE,"Could not inspect PCM process images");
    MODULEENTRY32W entry{};entry.dwSize=sizeof entry;
    if(Module32FirstW(snapshot,&entry))do{images.push_back(QString::fromWCharArray(entry.szModule));}while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);require(!images.empty(),"PCM process image enumeration was empty");
#elif defined(Q_OS_MACOS)
    for(std::uint32_t i=0;i<_dyld_image_count();++i)images.push_back(QString::fromUtf8(_dyld_get_image_name(i)));
#endif
    for(const auto& image:images)require(!image.contains("avcodec",Qt::CaseInsensitive) && !image.contains("ffmpegmediaplugin",Qt::CaseInsensitive),"PCM-only output loaded an FFmpeg codec runtime");
#endif
}
void conversion() {
    auto input=fixture(8000,1,3);
    for(int i=0;i<3;++i)qToLittleEndian<qint16>(std::array<qint16,3>{-32768,0,32767}[i],input.wav.data()+44+2*i);
    const auto original=input.wav;
    QAudioFormat format;format.setSampleRate(8000);format.setChannelCount(1);format.setSampleFormat(QAudioFormat::Int16);
    ResearchPcmSource direct(input,format);
    require(direct.readAll()==input.wav.sliced(44),"PCM output included a WAV header or changed canonical sample bytes");
    format.setSampleRate(16000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Float);
    ResearchPcmSource converted(input,format);
    auto bytes=converted.read(3);bytes+=converted.read(5);bytes+=converted.readAll();
    require(bytes.size()==6*2*4 && converted.atEnd() && converted.bytesAvailable()==0,"Finite PCM conversion lost partial frames or duration");
    const std::array<float,6> expected{-1,-.5f,0,32767.f/65536,32767.f/32768,32767.f/32768};
    for(unsigned i=0;i<12;++i){float value;std::memcpy(&value,bytes.constData()+4*i,4);require(std::abs(value-expected[i/2])<1e-6f,"Resampling/channel expansion changed independent PCM truth");}
    format.setSampleRate(8000);format.setChannelCount(1);format.setSampleFormat(QAudioFormat::UInt8);
    ResearchPcmSource unsignedSource(input,format);require(unsignedSource.readAll()==QByteArray::fromHex("0080ff"),"Unsigned PCM conversion has wrong bias or full scale");
    format.setSampleFormat(QAudioFormat::Int32);ResearchPcmSource integerSource(input,format);bytes=integerSource.readAll();
    const std::array<qint32,3> integers{std::numeric_limits<qint32>::min(),0,2147418112};
    for(unsigned i=0;i<3;++i){qint32 value;std::memcpy(&value,bytes.constData()+4*i,4);require(value==integers[i],"32-bit PCM output has wrong normalization");}
    auto stereo=fixture(8000,2,1);qToLittleEndian<qint16>(-16384,stereo.wav.data()+44);qToLittleEndian<qint16>(8192,stereo.wav.data()+46);
    format.setSampleFormat(QAudioFormat::Float);ResearchPcmSource mono(stereo,format);bytes=mono.readAll();float mixed;std::memcpy(&mixed,bytes.constData(),4);
    require(mixed==-.125f,"Stereo downmix changed independently known channel levels");
    require(input.wav==original,"Device format conversion changed the exportable WAV");
}
#endif
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
#if defined(AMC_TEST_AUDIO_OUTPUT) && QT_VERSION >= QT_VERSION_CHECK(6,8,0)
        const bool backendWasSet=qEnvironmentVariableIsSet("QT_MEDIA_BACKEND");
        const auto previousBackend=qgetenv("QT_MEDIA_BACKEND");
        qputenv("QT_MEDIA_BACKEND","ffmpeg");
#endif
        ResearchAudioPlayback player;QString error;std::vector<bool> states;
#if defined(AMC_TEST_AUDIO_OUTPUT) && QT_VERSION >= QT_VERSION_CHECK(6,8,0)
        require(qgetenv("QT_MEDIA_BACKEND")=="ffmpeg","PCM initialization changed the backend environment inherited by external tools");
        if(backendWasSet)qputenv("QT_MEDIA_BACKEND",previousBackend);else qunsetenv("QT_MEDIA_BACKEND");
#endif
        player.failed=[&](const QString& key){error=key;};
        player.playingChanged=[&](bool playing){states.push_back(playing);};
        player.setVolume(0);
        auto audio=fixture();const auto original=audio.wav;
        for(const auto& invalid:{QByteArray{},original.first(43),original.first(original.size()-1),QByteArray("not a WAV")}) {
            auto untrusted=audio;untrusted.wav=invalid;error.clear();player.play(untrusted);
            require(error=="invalid_audio" && !player.playing(),"Malformed independent-provider audio reached an output device");
        }
        player.stop();player.stop();require(states.empty(),"Stopped playback emitted a false start");
#ifdef AMC_TEST_AUDIO_OUTPUT
        conversion();
#endif
        if(application.arguments().contains("--output")) {
#ifdef AMC_TEST_AUDIO_OUTPUT
            const auto output=QMediaDevices::defaultAudioOutput();noCodecBackend();
            if(output.isNull()){std::cout<<"No native audio output device; validation passed, output qualification skipped\n";return 77;}
            for(const auto [rate,channels]:{std::pair{48000u,1u},std::pair{48000u,2u},std::pair{44100u,1u}}) {
                error.clear();states.clear();audio=fixture(rate,channels,rate/2);QElapsedTimer elapsed;elapsed.start();player.play(audio);
                if(!error.isEmpty() || !player.playing())throw std::runtime_error("Native PCM output could not start "+std::to_string(rate)+" Hz / "+std::to_string(channels)+" channel(s): "+error.toStdString());
                require(wait([&]{return !player.playing();},4000),"Finite PCM output did not drain and finish");
                require(elapsed.elapsed()>=250,"PCM output ended before its finite input could be presented");
                require(error.isEmpty() && states==std::vector<bool>{true,false},"Finite PCM completion or state transition failed");
                require(audio.wav==fixture(rate,channels,rate/2).wav,"Playback changed the exportable WAV");
                player.play(audio);require(player.playing(),"Completed PCM audio could not restart");
                player.stop();require(!player.playing(),"Explicit stop left PCM audio active");
                player.play(audio);require(player.playing(),"Stopped PCM audio could not restart");
                require(wait([&]{return !player.playing();},4000) && error.isEmpty(),"A stale completion stopped the replacement output or output failed");
                noCodecBackend();
            }
#else
            std::cout<<"Qt audio was not built; validation passed, output qualification skipped\n";return 77;
#endif
        }
        require(original==fixture().wav,"Validation changed the provider's WAV bytes");
        std::cout<<"Research PCM playback: malformed provider results, unchanged WAV, finite completion, stop and restart passed\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
