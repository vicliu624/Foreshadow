#include "signalscope/backend/sessions/monitor.hpp"
#include <iostream>
#include <future>
#include <thread>
using namespace signalscope;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
namespace {
Json test_stream(const Config& cfg,const BlockSink& sink,const std::function<bool()>& cancelled,const ReceiverReady& ready) {
    const Bytes block(cfg.block_bytes,128);
    std::uint64_t pairs=0;
    if(!cancelled()) {
        if(ready)ready({double(cfg.rate),double(cfg.frequency),1'536'000});
        sink(block,0);
        pairs=block.size()/4;
        if(cfg.device!=0)sink(block,pairs+1);
    }
    return {{"backend","test-source"},{"status","complete"},{"accepted_pairs",pairs}};
}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try {
        const auto info=inspect_recording(argv[1]);Config cfg;cfg.rate=info.rate;cfg.frequency=static_cast<std::uint32_t>(info.frequency);cfg.queue_blocks=1;cfg.analysis_queue_bytes=cfg.block_bytes;
        LoRaConfig lc;lc.rate=cfg.rate;lc.receiver_frequency=cfg.frequency;lc.sf_min=lc.sf_max=7;lc.workers=1;
        RetentionPolicy policy;policy.post_seconds=.01;policy.pre_seconds=.05;
        bool stalled=false;
        MonitorObserver observer;
        observer.analysis_progress=[&](std::uint64_t){if(!stalled){stalled=true;std::this_thread::sleep_for(std::chrono::milliseconds(250));}};
        const auto result=monitor(cfg,lc,policy,argv[2],argv[1],[]{return false;},observer);
        require(stalled,"test did not stall analysis after the preamble");
        require(result["analysis_dropped_pairs"].get<std::uint64_t>()>0,"capture must continue while analysis is stalled");
        require(result["capture_pipeline"]=="independent_capture_writer_v3","independent recording path missing");
        require(result["storage"]["completed"].get<unsigned>()>=1,"verified burst was not recorded");
        const auto verify_records=[&](const std::filesystem::path& root){
        std::ifstream original(info.data_path,std::ios::binary);
        for(const auto& item:std::filesystem::directory_iterator(root))if(item.is_directory()&&item.path().filename().string().starts_with("event-")){
            const auto recording=inspect_recording(item.path()/"capture.sigmf-meta");
            const auto& saved=recording.metadata["global"]["signalscope:result"];
            require(recording.metadata["global"]["signalscope:status"]!="analysis_gap","analysis gap must not split captured raw I/Q");
            const auto first=saved["stream_first_sample"].get<std::uint64_t>();
            Bytes expected(static_cast<std::size_t>(recording.pairs*2)),actual(expected.size());
            original.clear();original.seekg(static_cast<std::streamoff>(first*2));original.read(reinterpret_cast<char*>(expected.data()),static_cast<std::streamsize>(expected.size()));
            std::ifstream copied(recording.data_path,std::ios::binary);copied.read(reinterpret_cast<char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
            require(actual==expected,"recorded raw I/Q must be byte-exact despite analysis backpressure");
            require(saved["quiet_tail_observed"]==true,"quiet tail was not retained");
        }};
        verify_records(argv[2]);
        cfg.queue_blocks=64;
        cfg.analysis_queue_bytes=64*cfg.block_bytes;
        MonitorObserver no_detectors;
        no_detectors.enable_lora=false;
        const auto without=monitor(cfg,lc,policy,std::string(argv[2])+"-no-detectors",argv[1],
                                   []{return false;},no_detectors);
        require(without["processed_pairs"]==info.pairs && without["recent_lora_events"].empty() &&
                without["storage"]["completed"]==0,"disabled LoRa must not publish or record events");
        std::atomic<bool> writer_stalled=false,writer_waiting=false,analysis_advanced=false;
        MonitorObserver slow_writer;
        std::mutex pause_mutex;std::condition_variable pause_changed;
        bool analysis_synchronized=false;
        slow_writer.analysis_progress=[&](std::uint64_t){if(!analysis_synchronized){
            std::unique_lock lock(pause_mutex);
            require(pause_changed.wait_for(lock,std::chrono::seconds(10),[&]{return writer_waiting.load();}),"writer pause did not start");
            analysis_synchronized=true;
        }};
        slow_writer.recording_progress=[&](std::uint64_t){
            if(!writer_stalled.exchange(true)){
                std::unique_lock lock(pause_mutex);writer_waiting=true;pause_changed.notify_all();
                pause_changed.wait_for(lock,std::chrono::seconds(10),[&]{return analysis_advanced.load();});writer_waiting=false;
            }
        };
        slow_writer.status=[&](const Json& state){
            if(state.value("processed_pairs",0ULL)>=info.pairs){
                std::lock_guard lock(pause_mutex);
                if(writer_waiting){analysis_advanced=true;pause_changed.notify_all();}
            }
        };
        const std::filesystem::path slow_root=std::string(argv[2])+"-slow-writer";
        const auto writer_result=monitor(cfg,lc,policy,slow_root,argv[1],[]{return false;},slow_writer);
        require(writer_stalled&&analysis_advanced,"analysis did not advance during writer stall");
        require(writer_result["analysis_dropped_pairs"]==0,"writer stall must not discard analysis input");
        require(writer_result["recording_queue_high_water_blocks"].get<unsigned>()>1,"writer stall did not exercise bounded backlog");
        require(writer_result["recording_processed_pairs"]==writer_result["source_pairs"],"writer did not drain all accepted raw input");
        require(writer_result["storage"]["completed"].get<unsigned>()>=1,"writer-stalled burst was not saved");
        verify_records(slow_root);
        // 有界突发缓冲应允许分析暂停，而不是把未确认 I/Q 写成连续磁盘录制。
        const auto burst_input=std::filesystem::path(std::string(argv[2])+"-burst-input");
        std::filesystem::create_directories(burst_input);
        {
            std::ifstream in(info.data_path,std::ios::binary);Bytes original(static_cast<std::size_t>(info.pairs*2));
            in.read(reinterpret_cast<char*>(original.data()),static_cast<std::streamsize>(original.size()));
            std::ofstream out(burst_input/"capture.sigmf-data",std::ios::binary);
            for(int repeat=0;repeat<8;++repeat)out.write(reinterpret_cast<const char*>(original.data()),static_cast<std::streamsize>(original.size()));
        }
        write_json(burst_input/"capture.sigmf-meta",{{"global",{{"core:datatype","cu8"},{"core:sample_rate",info.rate},{"core:version","1.2.6"}}},
            {"captures",Json::array({{{"core:sample_start",0},{"core:frequency",info.frequency}}})},{"annotations",Json::array()}});
        cfg.analysis_queue_bytes=16*1024*1024;cfg.analysis_queue_seconds=5;
        bool burst_stalled=false;MonitorObserver burst_observer;
        burst_observer.analysis_progress=[&](std::uint64_t){if(!burst_stalled){
            burst_stalled=true;std::this_thread::sleep_for(std::chrono::seconds(2));}};
        const auto burst_root=std::filesystem::path(std::string(argv[2])+"-burst-buffer");
        const auto burst_result=monitor(cfg,lc,policy,burst_root,burst_input/"capture.sigmf-meta",[]{return false;},burst_observer);
        require(burst_stalled,"burst test did not suspend analysis");
        require(burst_result["analysis_dropped_pairs"]==0,"bounded burst buffer lost analysis samples");
        require(burst_result["processed_pairs"]==info.pairs*8,"burst buffer did not drain");
        require(burst_result["storage"]["completed"].get<unsigned>()>=1,"burst input produced no saved recording");
        require(burst_result["analysis_queue_high_water_blocks"].get<unsigned>()>64,"burst test did not exceed old analysis capacity");
        require(burst_result["analysis_queue_capacity_bytes"].get<std::uint64_t>()<=16*1024*1024,"analysis memory cap exceeded");
        const auto burst_info=inspect_recording(burst_input/"capture.sigmf-meta");
        for(const auto& item:std::filesystem::directory_iterator(burst_root))if(item.is_directory()&&item.path().filename().string().starts_with("event-")){
            const auto saved=inspect_recording(item.path()/"capture.sigmf-meta");const auto first=saved.metadata["global"]["signalscope:result"]["stream_first_sample"].get<std::uint64_t>();
            std::ifstream in(burst_info.data_path,std::ios::binary),out(saved.data_path,std::ios::binary);
            Bytes expected(static_cast<std::size_t>(saved.pairs*2)),actual(expected.size());in.seekg(static_cast<std::streamoff>(first*2));
            in.read(reinterpret_cast<char*>(expected.data()),static_cast<std::streamsize>(expected.size()));out.read(reinterpret_cast<char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
            require(actual==expected,"burst-buffer recording changed original I/Q");
        }
        MonitorObserver broken_writer;
        broken_writer.recording_progress=[](std::uint64_t){throw std::runtime_error("injected recording failure");};
        for(int attempt=0;attempt<5;++attempt){
        const std::filesystem::path broken_root=std::string(argv[2])+"-failed-writer-"+std::to_string(attempt);
        bool failed=false;
        try {monitor(cfg,lc,policy,broken_root,argv[1],[]{return false;},broken_writer);}
        catch(const std::runtime_error& error){failed=std::string(error.what())=="injected recording failure";}
        require(failed,"recording failure did not propagate or stop cleanly");
        std::ifstream failure_file(broken_root/"monitor-status.json");Json failure_status;failure_file>>failure_status;
        require(failure_status["state"]=="source_error","recording failure not persisted as source_error");
        }
        const ReceiverBackend backend{"live-test","test-source",nullptr,nullptr,test_stream,SampleFormat::ci16_le};
        const auto stride=bytes_per_pair(backend.format);
        const auto source_root=std::string(argv[2])+"-backend";
        Bytes audited,subscribed;
        std::uint64_t subscription_first=1;
        double observedBandwidth=0;
        MonitorObserver audit;
        audit.spectrum=[&](const SpectrumFrame& frame){observedBandwidth=frame.filter_bandwidth_hz;};
        audit.capture_iq.add([](const IqBlockView&){throw std::runtime_error("optional subscriber failed");});
        audit.capture_iq.add([&](const IqBlockView& block){
            require(!block.discontinuous && block.stream.sample_rate==cfg.rate && block.stream.center_frequency_hz==cfg.frequency &&
                    block.stream.format==backend.format,"live subscriber received invalid stream parameters");
            subscription_first=block.first_sample;
            subscribed.assign(block.bytes.begin(),block.bytes.end());
        });
        audit.audit_iq=[&](auto bytes,auto first){
            require(first==audited.size()/stride,"receiver audit sample position");
            audited.insert(audited.end(),bytes.begin(),bytes.end());
        };
        const auto live=monitor(cfg,lc,policy,source_root,{},[]{return false;},audit,backend);
        require(live["source"]=="live-test" && live["source_result"]["backend"]=="test-source",
                "monitor must use the selected receiver and its source identifier");
        require(live["sample_datatype"]=="ci16_le" && live["processed_pairs"]==cfg.block_bytes/stride &&
                audited==Bytes(cfg.block_bytes,128),
                "receiver I/Q must reach both analysis and recording unchanged");
        require(subscription_first==0 && subscribed==audited,
                "live subscriber must receive the same unmodified I/Q and absolute sample index");
        require(observedBandwidth==1'536'000,"The first live spectrum omitted the receiver filter readback");
        std::atomic<bool> stopRequested=false;
        std::promise<void> queued;
        auto ready=queued.get_future();
        MonitorObserver stopping;
        stopping.status=[&](const Json& status){
            if(status.at("state")=="starting")
                require(ready.wait_for(std::chrono::seconds(5))==std::future_status::ready,
                        "Test source did not enqueue its last block");
        };
        stopping.capture_iq.add([&](const IqBlockView&){
            stopRequested=true;
            queued.set_value();
        });
        const auto stopped=monitor(cfg,lc,policy,source_root+"-stop",{},[&]{return stopRequested.load();},stopping,backend);
        require(stopped["state"]=="stopped" && stopped["processed_pairs"]==cfg.block_bytes/stride &&
                stopped["recording_processed_pairs"]==stopped["processed_pairs"] && stopped["analysis_dropped_pairs"]==0,
                "Stop must drain the analysis blocks queued before the source stopped");
        cfg.seconds=double(cfg.block_bytes/stride)/cfg.rate;
        const auto manual_root=source_root+"-manual";
        Recorder manual(manual_root,cfg,backend.recording_source,backend.format);
        const auto capture=capture_receiver(backend,cfg,manual,[]{return false;});
        require(capture["accepted_pairs"]==cfg.block_bytes/stride && manual.pairs()==cfg.block_bytes/stride,
                "manual capture must use the selected receiver");
        cfg.device=1;
        bool gap=false;
        try {monitor(cfg,lc,policy,source_root+"-gap",{},[]{return false;},{},backend);}
        catch(const OperationError& error){gap=error.code()==ErrorCode::receiver_stream;}
        require(gap,"receiver discontinuity must fail before recording a misleading sample index");
        std::cout<<"分析与录制阻塞隔离通过：原始 I/Q 连续、逐字节一致，写盘路径停顿不阻塞分析\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
