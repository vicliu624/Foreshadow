#include "signalscope/adapters/modulation/snapshot_extension.hpp"
#include "signalscope/backend/sessions/snapshot_analysis.hpp"
#include <iostream>
#include <thread>
using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void drain(SnapshotAnalysis& analysis) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do{analysis.poll();if(!analysis.status().at("busy").get<bool>())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Snapshot provider did not finish");
}
}
int main() {
    try {
        constexpr std::uint32_t rate=2000000;constexpr std::uint64_t origin=1ULL<<40;
        std::atomic<unsigned> observed=0;
        registerSnapshotExtension({{"independent.periodic","independent.periodic",DetectorStage::snapshot,{},[](const DetectorContext&){return Json::object();}},
            [&](const SnapshotInput& input,Json& events){
                require(input.samples.size()==20000,"Provider observation duration was lost");
                require(input.first==origin+observed.load()*200000,"Periodic sample clock or arbitrary origin changed");
                ++observed;events.push_back({{"modulation_id","independent.periodic"},{"first_observed_sample",input.first}});
            },{SnapshotTrigger::periodic,.01,.1,1000000}});
        SnapshotSelection selected{{false,false},false,false,false};selected.extensions["independent.periodic"]=true;
        require(snapshotObservations(selected,500000).empty(),"Provider minimum rate was ignored");
        const auto policies=snapshotObservations(selected,rate);require(policies.size()==1 && policies[0].trigger==SnapshotTrigger::periodic,"Provider policy did not reach the generic registry");
        // The ordinary energy-trigger collector is visited first. Its silence
        // must not suppress a later periodic provider on an unchanging signal.
        selected.psk.bpsk=true;
        SnapshotAnalysis analysis;analysis.reset(selected);auto feed=analysis.makeFeed(rate);
        Bytes block(20000,140);const IqStreamParameters stream{rate,666000000,SampleFormat::cu8};
        for(unsigned i=0;i<100;++i){feed({block,origin+i*10000,stream,i==0});drain(analysis);}
        require(observed==5 && analysis.status().at("completed")==5,"Steady input did not produce periodic observations");
        require(analysis.status().at("offered_snapshot_bytes")==200000 && analysis.status().at("pending_bytes")==0,"Snapshot byte accounting is incorrect");
        const SnapshotObservation periodic{SnapshotTrigger::periodic,.01,.1,0};
        SnapshotCollector gap(rate,666000000,rate,SampleFormat::cu8,periodic);
        require(gap.feed(Bytes(10000,140),origin).bytes.empty(),"Partial periodic window was published");
        const auto post=gap.feed(Bytes(40000,170),origin+50000);
        require(post.first==origin+50000 && post.bytes==Bytes(40000,170),"Periodic window concatenated across an I/Q gap");
        {
            const auto filtered=snapshotObservationSelection(selected,periodic,rate);
            require(!filtered.psk.bpsk && filtered.extensions.at("independent.periodic"),"Periodic observation ran an energy-triggered detector");
            std::atomic<bool> entered=false,release=false;
            registerSnapshotExtension({{"independent.slow","independent.slow",DetectorStage::snapshot,{},[](const DetectorContext&){return Json::object();}},
                [&](const SnapshotInput&,Json&){entered=true;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                    while(!release && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));},periodic});
            auto slowSelection=SnapshotSelection{{false,false},false,false,false};slowSelection.extensions["independent.slow"]=true;
            SnapshotAnalysis slow;slow.reset(slowSelection);auto input=slow.makeFeed(rate);
            input({Bytes(40000,140),origin,stream,true});slow.poll();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while(!entered && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(entered,"Slow provider did not start");
            const auto started=std::chrono::steady_clock::now();
            for(unsigned i=2;i<100;++i){input({block,origin+i*10000,stream,false});slow.poll();}
            const auto elapsed=std::chrono::steady_clock::now()-started;const auto status=slow.status();release=true;drain(slow);
            require(elapsed<std::chrono::milliseconds(500),"Slow DSP blocked the input or status path");
            require(status.at("triggered")==5 && status.at("skipped_busy")==4 && status.at("skipped_snapshot_bytes")==160000 && status.at("pending_bytes")==0,"Overload did not account for skipped payloads");
            std::atomic<unsigned> fastObserved=0;entered=false;release=false;
            registerSnapshotExtension({{"independent.fast","independent.fast",DetectorStage::snapshot,{},[](const DetectorContext&){return Json::object();}},
                [&](const SnapshotInput&,Json&){++fastObserved;},{SnapshotTrigger::periodic,.005,.05,0}});
            slowSelection.extensions["independent.fast"]=true;slow.reset(slowSelection);input=slow.makeFeed(rate);
            for(unsigned i=0;i<100;++i) {
                input({block,origin+i*10000,stream,i==0});
                const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(300);
                bool pending=true;
                do {
                    slow.poll();pending=false;const auto progress=slow.status();
                    for(const auto& policy:progress.at("policies"))if(policy.at("duration_seconds")==.005)
                        pending=policy.at("triggered")!=policy.at("completed");
                    if(pending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }while(pending && std::chrono::steady_clock::now()<until);
                require(!pending,"Slow observation policy starved another policy");
            }
            const auto isolated=slow.status();release=true;drain(slow);
            require(fastObserved==10 && isolated.at("observation_policies")==2,"Independent periodic policies did not execute concurrently");
            for(const auto& policy:isolated.at("policies"))if(policy.at("duration_seconds")==.01)
                require(policy.at("completed")==0 && policy.at("inflight_bytes")==40000,"Slow provider was not held independently");
        }
        bool rejected=false;
        try{registerSnapshotExtension({{"future.protocol","future.protocol",DetectorStage::snapshot,{},[](const DetectorContext&){return Json::object();}},[](const SnapshotInput&,Json&){},{},snapshotExtensionProtocolVersion+1});}
        catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Unknown snapshot lifecycle protocol was accepted");
        std::cout<<"Independent periodic provider, steady input, mixed triggers, arbitrary origins, gaps and byte accounting passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
