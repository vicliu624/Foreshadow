#include "plugins/desktop/research/builtin/dtmb_tv.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <iostream>
#include <thread>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("SignalScopeTests");app.setApplicationName("DtmbLiveInput");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try {
        require(app.arguments().size()==3,"Expected media helper and independent SigMF metadata");
        const auto info=inspect_recording(path_from_utf8(app.arguments()[2].toUtf8().toStdString()));
        const IqStreamParameters parameters{info.rate,info.frequency,info.format};const auto stride=bytes_per_pair(info.format);
        require(info.pairs>info.rate/2,"The live-input qualification requires a complete positive recording");
        auto provider=dtmbTvResearchProvider(path_from_utf8(app.arguments()[1].toUtf8().toStdString()));QWidget owner;auto tool=provider.create("en",&owner);
        tool.widget->resize(1200,780);tool.widget->show();auto* source=tool.widget->findChild<QComboBox*>("dtmb-tv-source");
        auto* start=tool.widget->findChild<QPushButton*>("dtmb-tv-start");require(source && start,"Live TV controls were missing");
        source->setCurrentIndex(source->findData("live"));tool.sourceChanged({true,parameters});
        tool.widget->findChild<QCheckBox*>("dtmb-tv-mute")->setChecked(true);require(start->isEnabled(),"The independent input was rejected for live reception");start->click();
        std::atomic_bool finished=false;std::atomic<std::uint64_t> first=0,witness=0,maximumOfferUs=0;std::exception_ptr sourceFailure;
        IqSubscribers subscribers;subscribers.add(tool.offer);subscribers.add([&](const IqBlockView& block){witness+=block.bytes.size()/stride;});
        std::jthread producer([&](std::stop_token stop){
            try {
                IqPublisher publisher(subscribers,parameters);std::ifstream input(info.data_path,std::ios::binary);require(bool(input),"Could not open the independent input");
                Bytes bytes(65536*stride);const auto began=std::chrono::steady_clock::now();const auto target=std::uint64_t(parameters.sample_rate)*8;
                while(!stop.stop_requested() && first<target) {
                    input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());const auto size=static_cast<std::size_t>(input.gcount());
                    if(!size){input.clear();input.seekg(0);continue;}
                    const auto before=std::chrono::steady_clock::now();publisher.publish(std::span(bytes).first(size),first.load());
                    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();
                    maximumOfferUs=std::max(maximumOfferUs.load(),static_cast<std::uint64_t>(elapsed));first+=size/stride;
                    std::this_thread::sleep_until(began+std::chrono::microseconds(first.load()*1000000/parameters.sample_rate));
                }
                // Deliberate overload is separate from the paced portion. The
                // capture timeline and a second subscriber remain continuous.
                for(unsigned i=0;i<512 && !stop.stop_requested();++i) {
                    input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());auto size=static_cast<std::size_t>(input.gcount());
                    if(!size){input.clear();input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());size=input.gcount();}
                    require(size>0,"The overload source ended unexpectedly");
                    const auto before=std::chrono::steady_clock::now();publisher.publish(std::span(bytes).first(size),first.load());
                    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();
                    maximumOfferUs=std::max(maximumOfferUs.load(),static_cast<std::uint64_t>(elapsed));first+=size/stride;
                }
            }catch(...){sourceFailure=std::current_exception();}
            finished=true;
        });
        QElapsedTimer timer;timer.start();std::uint64_t heartbeats=0,maximumVideo=0,maximumAudio=0;Json state,positive;
        while(timer.elapsed()<20000) {
            QApplication::processEvents();QThread::msleep(5);++heartbeats;state=tool.snapshot();const auto& player=state.at("playback");
            const auto frames=player.value("presented_video_frames",0ULL);maximumVideo=std::max<std::uint64_t>(maximumVideo,frames);
            maximumAudio=std::max<std::uint64_t>(maximumAudio,player.value("decoded_audio_frames",0ULL));
            if(frames)positive=state;
            if(finished && timer.elapsed()>11000)break;
        }
        producer.request_stop();producer.join();if(sourceFailure)std::rethrow_exception(sourceFailure);
        state=tool.snapshot();const auto stopBegan=std::chrono::steady_clock::now();tool.stop();
        const auto stopMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-stopBegan).count();
        const auto& input=state.at("input");
        const Json proof={{"scope","Looped independent short IQ, paced for eight seconds then deliberately overloaded; not a long independent broadcast or hardware no-loss proof"},
            {"source_pairs",first.load()},{"witness_pairs",witness.load()},{"maximum_offer_us",maximumOfferUs.load()},{"gui_heartbeats",heartbeats},
            {"maximum_presented_video_frames",maximumVideo},{"maximum_decoded_audio_frames",maximumAudio},{"stop_ms",stopMs},{"last_state",state},{"positive_state",positive}};
        std::cout<<proof.dump(2)<<'\n';
        require(witness==first && first>=std::uint64_t(parameters.sample_rate)*8,"The optional TV consumer blocked or lost data for the independent subscriber");
        require(!state.contains("diagnostic") && !input.at("done").get<bool>() && input.at("received_pairs")==first.load(),
            "Sustained input or queue pressure terminated the live processing generation");
        require(maximumOfferUs<100000 && heartbeats>200,"Live TV blocked capture delivery or the GUI");
        require(input.at("queue_peak_bytes")<=input.at("budget_bytes") && input.at("rejected_pairs").get<std::uint64_t>()>0,
            "Deliberate overload exceeded the raw-IQ byte budget or was not counted");
        require(maximumVideo>0 && maximumAudio>0,"The actual live subscription path never recovered media from the positive recording");
        require(stopMs<5000 && !tool.busy(),"Stopping live television left its processing generation running");
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
