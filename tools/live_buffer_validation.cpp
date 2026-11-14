#include "signalscope/backend/sessions/monitor.hpp"
#include <iostream>
#include <thread>
#include <csignal>
#include <cmath>
using namespace signalscope;
namespace {
volatile std::sig_atomic_t cancelled=0;
void stop(int){cancelled=1;}
double unix_seconds(){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
int main(int argc,char** argv){
    try {
        if(argc<2||std::string(argv[1])!="monitor")throw std::invalid_argument("仅用于有限 monitor 验证");
        Config cfg;cfg.seconds=45;LoRaConfig lc;lc.workers=4;lc.peak_to_mean_db=11;
        RetentionPolicy policy;std::filesystem::path output;
        for(int i=2;i<argc;i+=2){
            if(i+1>=argc)throw std::invalid_argument("验证选项缺少值");
            const std::string key=argv[i],value=argv[i+1];
            if(key=="--output")output=value;
            else if(key=="--seconds")cfg.seconds=std::stod(value);
            else if(key=="--frequency")cfg.frequency=static_cast<std::uint32_t>(std::stoul(value));
            else if(key=="--rate")cfg.rate=static_cast<std::uint32_t>(std::stoul(value));
            else if(key=="--gain"){cfg.auto_gain=value=="auto";if(!cfg.auto_gain)cfg.gain_tenths=static_cast<int>(std::lround(std::stod(value)*10));}
            else if(key=="--workers")lc.workers=static_cast<unsigned>(std::stoul(value));
            else if(key=="--threshold-db")lc.peak_to_mean_db=std::stod(value);
            else if(key=="--quota-mib")policy.quota_bytes=std::stoull(value)*1048576;
            else throw std::invalid_argument("未知验证选项："+key);
        }
        if(output.empty()||!std::isfinite(cfg.seconds)||cfg.seconds<10||cfg.seconds>60)
            throw std::invalid_argument("硬件压力验证必须指定输出，且时长在 10–60 秒内");
        cfg.validate();lc.rate=cfg.rate;lc.receiver_frequency=cfg.frequency;lc.bandwidth_candidates={125000,250000,500000};
        bool owner_written=false,injected=false;Json pause=Json::object();
        MonitorObserver observer;
        observer.status=[&](const Json& status){
            if(status.value("state",std::string())!="listening")return;
            if(!owner_written){write_json(output/"forced-validation-owner.json",{{"owner","signalscope-live-buffer-test-v1"},{"pause_ms",2500}});owner_written=true;}
            if(injected||status.value("recent_lora_events",Json::array()).empty())return;
            injected=true;pause={{"state","paused"},{"begin_unix_seconds",unix_seconds()},{"pause_ms",2500},
                {"processed_at_pause",status.value("processed_pairs",0ULL)}};
            write_json(output/"analysis-pause.json",pause);
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            pause["end_unix_seconds"]=unix_seconds();pause["state"]="resumed";write_json(output/"analysis-pause.json",pause);
        };
        std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        auto report=monitor(cfg,lc,policy,output,{},[]{return cancelled!=0;},observer);
        const bool passed=injected&&report.value("analysis_dropped_pairs",1ULL)==0&&report.value("analysis_gaps",1ULL)==0&&
            report.value("result_dropped_groups",1ULL)==0&&report.value("result_dropped_events",1ULL)==0&&
            report.value("source_pairs",0ULL)==sample_count(cfg.seconds,cfg.rate)&&
            report["processed_pairs"]==report["source_pairs"]&&report["recording_processed_pairs"]==report["source_pairs"]&&
            report.value("analysis_queue_high_water_blocks",0u)>64;
        report["forced_pause"]=pause;report["forced_buffer_validation_passed"]=passed;
        write_json(output/"forced-result.json",report);std::cout<<report.dump(2)<<'\n';return passed?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
