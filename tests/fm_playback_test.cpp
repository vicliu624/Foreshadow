#include "plugins/desktop/research/builtin/fm_playback.hpp"
#include <QCoreApplication>
#include <QTimer>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <thread>
using namespace signalscope;
using namespace signalscope::desktop;
namespace {
constexpr std::uint32_t rate=10000000;
Bytes waveform() {
    Bytes bytes;bytes.reserve(rate/10*4);double phase=0;std::mt19937 random(20261110);std::normal_distribution<double> noise(0,.03);
    for(std::uint32_t i=0;i<rate/10;++i) {
        phase+=2*std::numbers::pi*(-3000000+15000*std::sin(2*std::numbers::pi*1000*i/rate))/rate;
        const auto value=std::polar(.25,phase)+std::complex<double>(noise(random),noise(random));
        for(const auto part:{value.real(),value.imag()}){const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(part*32768)));bytes.push_back(word&255);bytes.push_back(word>>8);}
    }return bytes;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        const auto devices=FmPlayback::outputDevices();
        if(!FmPlayback::supported() || std::none_of(devices.begin(),devices.end(),[](const auto& d){return d.system_default && d.supports_pcm;})){
            std::cout<<"No compatible default audio output; physical playback test skipped.\n";return 77;
        }
        const auto bytes=waveform();FmPlayback audio;audio.setVolume(0);
        try{audio.start({rate,SampleFormat::ci16_le,-3000000},65536);}catch(const FmPlaybackError& e){
            if(e.key.starts_with("output_") || e.key=="audio_unavailable"){std::cout<<"Audio output could not be opened: "<<e.key<<'\n';return 77;}throw;
        }
        std::jthread producer([&](std::stop_token stop){
            auto started=std::chrono::steady_clock::now();std::uint64_t first=1ULL<<40;bool paused=false;unsigned sequence=0;
            for(int repeat=0;repeat<40 && !stop.stop_requested();++repeat)for(std::size_t at=0;at<bytes.size() && !stop.stop_requested();) {
                if(!paused && first-(1ULL<<40)>=18000000){std::this_thread::sleep_for(std::chrono::milliseconds(300));started+=std::chrono::milliseconds(300);paused=true;}
                const auto size=std::min<std::size_t>((sequence++%3==0?448:16384)*4,bytes.size()-at);
                audio.offer(std::span(bytes).subspan(at,size),first);first+=size/4;at+=size;
                std::this_thread::sleep_until(started+std::chrono::nanoseconds((first-(1ULL<<40))*100));
            }
        });
        bool closed=false,reopened=false,muted=false,switched=false,backgroundProgress=false;std::uint64_t beforeSwitch=0;
        QTimer::singleShot(800,&app,[&]{
            const auto before=audio.status().written_frames;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            backgroundProgress=audio.status().written_frames>=before+4800;
        });
        QTimer::singleShot(500,&app,[&]{audio.setMuted(true);muted=audio.status().muted;audio.setMuted(false);audio.setSquelch(60);});
        QTimer::singleShot(1200,&app,[&]{closed=audio.status().squelched;audio.setSquelch(6);});
        QTimer::singleShot(1600,&app,[&]{reopened=!audio.status().squelched;});
        QTimer::singleShot(2600,&app,[&]{try{const auto state=audio.status();beforeSwitch=state.written_frames;audio.setOutputDevice(state.device_id);switched=true;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';app.exit(2);}});
        QTimer check;check.setInterval(10);QObject::connect(&check,&QTimer::timeout,[&]{
            const auto status=audio.status();if(status.state=="failed"){std::cerr<<status.error_key<<' '<<status.error_detail<<'\n';app.exit(2);}
        });check.start();QTimer::singleShot(4200,&app,&QCoreApplication::quit);
        const auto code=app.exec();producer.request_stop();producer.join();const auto status=audio.status();audio.stop();
        std::cout<<Json{{"state",status.state},{"underruns",status.underruns},{"written_frames",status.written_frames},
            {"input_pairs",status.input.input_pairs},{"processed_pairs",status.input.processed_pairs},{"dropped_pairs",status.input.dropped_pairs},
            {"queue_peak_bytes",status.input.queue_high_water_bytes},{"squelch_closed",closed},{"squelch_reopened",reopened},{"muted",muted},{"output_reopened",switched},{"output_during_ui_stall",backgroundProgress}}.dump()<<'\n';
        if(code || !status.error_key.empty() || status.written_frames<100000 || !closed || !reopened || !muted || !switched || !backgroundProgress || status.written_frames<=beforeSwitch)
            throw std::runtime_error("FM playback, recovery, live controls or output reopening failed");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
