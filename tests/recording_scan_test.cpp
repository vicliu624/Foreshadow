#include "signalscope/backend/recordings/recording_scan.hpp"
#include "signalscope/backend/sessions/replay_session.hpp"
#include "signalscope/adapters/modulation/snapshot_extension.hpp"
#include <chrono>
#include <iostream>

int main() {
    using namespace signalscope;
    const auto directory=std::filesystem::temp_directory_path()/
        ("amc-scan-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Config config;
        config.rate=1000000;
        config.seconds=.39;
        {
            Recorder recorder(directory,config,"synthetic-test",SampleFormat::ci16_le);
            recorder.append(Bytes(390000*4,0));
            recorder.finish("complete");
        }
        const auto info=inspect_recording(directory/"capture.sigmf-meta");
        const RecordingScanOptions options{17,380000,500000,{{false,false},false,true,false}};
        const auto hop=config.rate/8;
        const std::vector<std::pair<std::uint64_t,std::uint64_t>> expected{
            {17,17+2*hop},{17+hop,17+3*hop},{17+2*hop,380017}};
        const auto run=[&](bool cancelAfterFirst) {
            std::size_t seen=0;
            scanRecording(info,options,[&](const Json& window){
                if(seen>=expected.size() || window.at("first_sample")!=expected[seen].first ||
                   window.at("end_sample_exclusive")!=expected[seen].second ||
                   !window.at("events").empty())
                    throw std::runtime_error("Unexpected scan window or evidence");
                ++seen;
            },[&]{return cancelAfterFirst && seen>0;});
            return seen;
        };
        if(run(false)!=expected.size() || run(true)!=1)
            throw std::runtime_error("Scan did not reach the requested end or stop on cancellation");
        ReplaySessionOptions replayOptions;
        replayOptions.enableLora=false;
        replayOptions.lora.search_width=400000;
        replayOptions.narrowband={false,false,false,false};
        Json finalStatus;
        unsigned pausePolls=0;
        std::uint64_t consumed=0;
        ReplaySessionObserver replayObserver;
        bool spectrumReceived=false;
        replayObserver.spectrum=[&](const SpectrumFrame& frame){spectrumReceived=frame.search_width_hz==400000;};
        replayObserver.status=[&](const Json& status){finalStatus=status;};
        replayObserver.analysis_iq.add([](const IqBlockView&){throw std::runtime_error("optional subscriber failed");});
        replayObserver.analysis_iq.add([&](const IqBlockView& block){
            if(pausePolls<3 || block.first_sample!=0 || block.bytes.empty() || block.discontinuous)
                throw std::runtime_error("Replay delivered a block before resuming or lost its sample index");
            if(block.stream.sample_rate!=info.rate || block.stream.center_frequency_hz!=info.frequency ||
               block.stream.format!=info.format)
                throw std::runtime_error("Replay subscriber received invalid stream parameters");
            consumed=block.bytes.size()/bytes_per_pair(block.stream.format);
        });
        replaySession(info,replayOptions,[&]{return ++pausePolls<3;},[&]{return consumed>0;},replayObserver);
        if(finalStatus.value("state","")!="stopped" || finalStatus.at("processed_pairs")!=consumed ||
           finalStatus.at("source_pairs")!=info.pairs || consumed==0 || consumed>=info.pairs || !spectrumReceived)
            throw std::runtime_error("Cancelled replay did not publish its actual progress");
        registerSnapshotExtension({{"scan.fixture","scan.fixture",DetectorStage::snapshot,{},[](const DetectorContext&){return Json::object();}},
            [](const SnapshotInput& input,Json& events){events.push_back({{"first",input.first},{"count",input.samples.size()}});}});
        for(const auto format:{SampleFormat::ci16_le,SampleFormat::cf32_le}) {
            const auto folder=directory/sample_datatype(format);Config wide;wide.rate=16000000;
            {Recorder writer(folder,wide,"wideband-scan-fixture",format);writer.append(Bytes(6200000*bytes_per_pair(format),0));writer.finish("complete");}
            RecordingScanOptions scan;scan.first=37;scan.count=6000000;scan.search_width=16000000;
            scan.detectors={{false,false},false,false,false};scan.detectors.extensions["scan.fixture"]=true;
            std::size_t windows=0;const std::vector<std::pair<std::uint64_t,std::uint64_t>> coordinates{{37,4000037},{2000037,6000037}};
            scanRecording(inspect_recording(folder/"capture.sigmf-meta"),scan,[&](const Json& result){
                if(windows>=coordinates.size() || result.at("first_sample")!=coordinates[windows].first || result.at("end_sample_exclusive")!=coordinates[windows].second ||
                   result.at("events").size()!=1 || result.at("events")[0].at("first")!=coordinates[windows].first)
                    throw std::runtime_error("Wideband replay chunks lost scan window boundaries or source coordinates");
                ++windows;
            },[]{return false;});
            if(windows!=coordinates.size())throw std::runtime_error("Wideband scan did not complete");
        }
        std::filesystem::remove_all(directory);
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
