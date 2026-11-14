#include "plugins/desktop/research/media_playback.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <iostream>
#include <thread>
using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json wait(MediaPlayback& player,unsigned milliseconds) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<milliseconds) {
        QCoreApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto state=player.snapshot();const auto name=state.value("state",std::string());
        if(name=="finished" || name=="error")return state;
    }
    return player.snapshot();
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(app.arguments().size()>=2,"Expected the media helper path");
        const bool file=app.arguments().size()>2;
        MediaPlayback player;player.setVolume(.2);player.setMuted(true);
        player.start({101},path_from_utf8(app.arguments()[1].toUtf8().toStdString()));
        std::jthread producer;
        if(file) {
            player.start({},path_from_utf8(app.arguments()[1].toUtf8().toStdString()));
            producer=std::jthread([&](std::stop_token stop){
                std::ifstream input(path_from_utf8(app.arguments()[2].toUtf8().toStdString()),std::ios::binary);Bytes bytes(16384);
                while(!stop.stop_requested() && (input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) || input.gcount())) {
                    const auto part=std::span(bytes).first(static_cast<std::size_t>(input.gcount()));
                    while(!stop.stop_requested() && !player.offerTransport(part)) {
                        if(player.snapshot().value("state",std::string())=="error")return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                player.finishInput();
            });
        }else {
            Bytes packets(188*3,0xff);for(unsigned i=0;i<3;++i)packets[i*188]=0x47;
            require(player.offerTransport(packets),"Could not offer a bounded fixture input");player.finishInput();
        }
        Json before;
        if(!file) {
            // Test independence after playback has actually started. Native
            // device/helper initialization can exceed a fixed startup sleep;
            // an empty snapshot cannot establish a running-stream baseline.
            QElapsedTimer startup;startup.start();
            do {
                QCoreApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(5));before=player.snapshot();
                if(before.value("state",std::string())=="error")break;
            }while(startup.elapsed()<5000 && (before.value("decoded_audio_frames",0ULL)==0 || before.value("presented_video_frames",0ULL)==0));
            require(before.value("decoded_audio_frames",0ULL)>0 && before.value("presented_video_frames",0ULL)>0 && before.value("state",std::string())!="finished",("The media fixture did not enter active playback: "+before.dump()).c_str());
        }else {std::this_thread::sleep_for(std::chrono::milliseconds(200));before=player.snapshot();}
        // Deliberately stall the thread that would own desktop widgets. No
        // event processing is allowed between these two running snapshots.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));const auto after=player.snapshot();
        if(!file) {
            std::cout<<Json{{"gui_stall_audio_before",before.value("decoded_audio_frames",0ULL)},{"gui_stall_audio_after",after.value("decoded_audio_frames",0ULL)},{"gui_stall_presented_after",after.value("presented_video_frames",0ULL)}}.dump()<<'\n';
            require(after.value("decoded_audio_frames",0ULL)>before.value("decoded_audio_frames",0ULL),("Media decoding stopped while the GUI thread was stalled; before="+before.dump()+"; after="+after.dump()).c_str());
            require(after.value("presented_video_frames",0ULL)>before.value("presented_video_frames",0ULL),"The display clock did not progress independently of the GUI thread");
        }
        auto state=wait(player,file?25000:5000);std::cout<<state.dump(2)<<'\n';
        require(state.at("state")=="finished","The player failed or did not drain at EOF");
        require(state.at("decoded_audio_frames").get<std::uint64_t>()>0 && state.at("presented_video_frames").get<std::uint64_t>()>0,"Decoded media was not presented");
        require(state.at("written_audio_frames").get<std::uint64_t>()>0 || state.at("unavailable_audio_frames").get<std::uint64_t>()>0,"Audio output availability was not reported");
        if(!file && state.at("audio_clock_video_samples").get<std::uint64_t>()>0)
            require(state.at("maximum_audio_video_offset_us").get<std::int64_t>()<100000,"Video was not scheduled against the audio output clock");
        require(state.at("volume")==.2 && state.at("muted")==true,"Volume/mute controls did not persist");
        auto frame=player.videoFrame();require(frame && !frame->image.isNull(),"No actual video image was retained");
        if(file)frame->image.save(app.arguments().size()>3?app.arguments()[3]:"media-playback-proof.png");
        else {
            require(frame->image.pixelColor(0,0).red()==16,"Video pixels were not decoded from the framed protocol");
            const auto oldGeneration=frame->generation;player.select({202});
            state=wait(player,5000);frame=player.videoFrame();
            require(state.at("state")=="finished" && frame && frame->generation>oldGeneration && frame->image.pixelColor(0,0).red()==200,"Program change retained an old helper or image generation");
            require(player.cachedTransport().size()==188*3,"Cached transport export lost complete packet framing");
            player.setOutputDevice("an-output-device-that-is-not-installed");
            player.start({101},path_from_utf8(app.arguments()[1].toUtf8().toStdString()));
            Bytes packets(564,0xff);for(unsigned i=0;i<3;++i)packets[i*188]=0x47;
            require(player.offerTransport(packets),"The unavailable-device test could not accept input");player.finishInput();state=wait(player,5000);
            require(state.at("state")=="finished" && state.at("unavailable_audio_frames")==38400 && state.at("audio_queue_bytes")==0 &&
                    state.at("presented_video_frames").get<unsigned>()>0 && !state.at("audio_error").empty(),
                "An unavailable audio device blocked video or EOF, or silently discarded audio");
            for(const auto program:{303u,404u}) {
                player.start({program},path_from_utf8(app.arguments()[1].toUtf8().toStdString()));require(player.offerTransport(packets),"The failed-helper test could not accept input");player.finishInput();
                state=wait(player,5000);require(state.at("state")=="error" && !state.at("diagnostic").empty() && !player.videoFrame(),"A helper crash or corrupt protocol did not invalidate its playback generation");
            }
            player.start({202},path_from_utf8(app.arguments()[1].toUtf8().toStdString()));require(player.offerTransport(packets),"Playback could not restart after a failed helper");player.finishInput();
            state=wait(player,5000);require(state.at("state")=="finished" && player.videoFrame(),"A previous helper error poisoned a new playback generation");
            player.setOutputDevice("");
            for(const auto program:{505u,606u}) {
                // Leave input open: audio can finish or pause before an RF/TS
                // source stops, so pipe EOF cannot be the classification gate.
                player.start({program},path_from_utf8(app.arguments()[1].toUtf8().toStdString()));require(player.offerTransport(packets),"The audio continuity fixture could not accept input");
                bool observedWait=false;QElapsedTimer pending;pending.start();
                while(pending.elapsed()<2000) {
                    QCoreApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(5));state=player.snapshot();
                    observedWait=observedWait || state.value("audio_output_waiting",false);
                    if(state.value("state",std::string())=="finished" || state.value("state",std::string())=="error")break;
                }
                state=wait(player,5000);
                require(state.at("state")=="finished","The interrupted audio or shorter-audio tail did not finish");
                std::cout<<Json{{"audio_fixture_program",program},{"underruns",state.at("audio_underruns")},{"eof_drains",state.at("audio_eof_drains")},{"written",state.at("written_audio_frames")}}.dump()<<'\n';
                if(state.at("written_audio_frames").get<unsigned>()>0)require(observedWait && (program==505?state.at("audio_underruns").get<unsigned>()>0:state.at("audio_eof_drains").get<unsigned>()>0),
                    "A resumed audio outage was confused with normal audio ending before video");
            }
        }
        if(producer.joinable()){producer.request_stop();producer.join();}
        player.stop();require(player.snapshot().at("state")=="stopped" && !player.videoFrame(),"Stop left a stale playback image active");
        std::cout<<"Independent media output, GUI stalls, EOF drain, image pixels, selection generations, volume and stop passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
