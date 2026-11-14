#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/backend/sessions/monitor.hpp"
#include "signalscope/adapters/modulation/wideband_lora.hpp"
#include "signalscope/backend/sessions/receiver_configuration.hpp"
#include "signalscope/backend/recordings/event_store.hpp"
#include "signalscope/backend/recordings/recording_scan.hpp"
#include "signalscope/backend/recordings/recording_export.hpp"
#include "signalscope/backend/sessions/replay_session.hpp"
#include "signalscope/adapters/modulation/fm_audio.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/distribution/distribution.hpp"
#include <algorithm>
#include <cmath>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <set>

using namespace signalscope;
namespace {
volatile std::sig_atomic_t interrupted=0;
void on_signal(int) { interrupted=1; }
bool cancelled() { return interrupted!=0; }
class Args {
public:
    Args(int argc,char** argv) {
        for(int n=2;n<argc;++n) {
            std::string key=argv[n];
            if(!key.starts_with("--") || n+1==argc || values.contains(key)) throw std::invalid_argument("Options require a single value and must not repeat: "+key);
            values.emplace(key,argv[++n]);
        }
    }
    std::string get(const std::string& key,const std::string& fallback="") { auto it=values.find(key); if(it==values.end()) return fallback; auto v=it->second; values.erase(it); return v; }
    std::string required(const std::string& key) { auto v=get(key); if(v.empty()) throw std::invalid_argument("Missing argument: "+key); return v; }
    double real(const std::string& key,double fallback) {
        auto text=get(key); if(text.empty()) return fallback;
        std::size_t end=0; const auto v=std::stod(text,&end);
        if(end!=text.size() || !std::isfinite(v)) throw std::invalid_argument("Invalid number: "+key); return v;
    }
    std::uint64_t integer(const std::string& key,std::uint64_t fallback,std::uint64_t maximum) {
        const auto v=real(key,double(fallback));
        if(v<0 || v>double(maximum) || std::floor(v)!=v) throw std::invalid_argument("Integer out of range: "+key); return static_cast<std::uint64_t>(v);
    }
    void finish() { if(!values.empty()) throw std::invalid_argument("Unknown argument: "+values.begin()->first); }
private: std::map<std::string,std::string> values;
};
NarrowbandSelection narrowbandFlags(Args& args,bool monitor) {
    NarrowbandSelection selected;
    selected.fsk=args.integer("--fsk",monitor?0:1,1)!=0;
    // For monitor, the historic --fsk switch also sets the defaults for the other modes.
    const bool fallback=!monitor || selected.fsk;
    for(const auto& option:narrowband_options) {
        if(option.enabled==&NarrowbandSelection::fsk)continue;
        selected.*option.enabled=args.integer(std::string("--")+option.id,fallback,1)!=0;
    }
    return selected;
}
void bandwidths(Args& args,LoRaConfig& config) {
    const auto text=args.get("--bw","all");
    if(text=="all") {config.bandwidth_candidates={125000,250000,500000};return;}
    std::size_t end=0;const auto value=std::stod(text,&end);
    if(end!=text.size() || !std::isfinite(value))throw std::invalid_argument("Use all or a bandwidth in Hz for --bw");
    config.bandwidth=value;
}
Config config(Args& args,double default_seconds=10,const char* default_gain="19.7") {
    Config c;
    c.seconds=default_seconds;
    c.rate=static_cast<std::uint32_t>(args.integer("--rate",c.rate,100000000));
    c.frequency=static_cast<std::uint64_t>(args.integer("--frequency",c.frequency,9007199254740991ULL));
    c.seconds=args.real("--seconds",c.seconds);
    c.device=static_cast<std::uint32_t>(args.integer("--device",0,1000));
    c.antenna=args.get("--antenna");
    c.device_serial=args.get("--serial");
    c.input_mode=args.get("--input-mode","auto");
    c.if_bandwidth_hz=args.real("--if-bandwidth",0);
    c.receiver_settings=Json::parse(args.get("--settings","{}"));
    if(!c.receiver_settings.is_object())throw std::invalid_argument("--settings requires a JSON object");
    c.block_bytes=static_cast<std::size_t>(args.integer("--block-bytes",c.block_bytes,4194304));
    c.queue_blocks=static_cast<std::size_t>(args.integer("--queue-blocks",c.queue_blocks,4096));
    c.analysis_queue_bytes=static_cast<std::size_t>(args.integer("--analysis-queue-mib",c.analysis_queue_bytes/1048576,128)*1048576);
    c.analysis_queue_seconds=args.real("--analysis-queue-seconds",c.analysis_queue_seconds);
    c.settle_seconds=args.real("--settle",c.settle_seconds);
    const auto gain=args.get("--gain",default_gain);
    c.auto_gain=gain=="auto";
    if(!c.auto_gain) {
        std::size_t end=0; auto g=std::stod(gain,&end);
        if(end!=gain.size() || !std::isfinite(g) || g<-100 || g>100) throw std::invalid_argument("Invalid gain");
        c.gain_tenths=static_cast<int>(std::lround(g*10));
    }
    c.validate(); return c;
}
SnapshotSelection scanSelection(std::string_view names) {
    const auto options=snapshotDetectorOptions();
    SnapshotSelection selection;
    const bool all=names=="all";
    for(const auto& option:options)option.enabled(selection)=all;
    if(all)return selection;
    for(;;) {
        const auto end=names.find(',');
        const auto name=names.substr(0,end);
        const auto option=std::find_if(options.begin(),options.end(),[name](const auto& entry){return name==entry.id;});
        if(option==options.end())throw std::invalid_argument("Unknown scan modulation: "+std::string(name));
        option->enabled(selection)=true;
        if(end==std::string_view::npos)break;
        names.remove_prefix(end+1);
    }
    return selection;
}
void help() {
    std::cout << "Foreshadow " << distribution::applicationVersion() << " · Receive, record and replay I/Q\n"
        "  devices, probe, capture and monitor accept --receiver ID. Available IDs:";
    for(const auto* receiver:receiver_backends())std::cout<<' '<<receiver->id;
    std::cout<<"\n  Receiver commands accept --antenna PORT and --serial SERIAL when supported by the plugin.\n"
        "  Use probe and parameters to list device capabilities and controls.\n"
        "  devices\n"
        "  probe [--device INDEX]\n"
        "  validate [--purpose monitor|capture --rate Hz --frequency Hz --gain DB_OR_AUTO --antenna PORT]\n"
        "           [--search-width Hz --bw all|Hz --lora 0|1 --fsk 0|1 --sf-min 7 --sf-max 12]\n"
        "    Reports attributed device capabilities and hardware/analysis compatibility as JSON.\n"
        "  receiver-plugins  List loaded/rejected receiver modules and ABI diagnostics.\n"
        "  parameters, recommend [--receiver ID]  Describe controls or proposed configurations.\n"
        "  Live commands accept --settings JSON for plugin-specific settings.\n"
        "  configure [--rate Hz --frequency Hz --gain DB_OR_AUTO --antenna PORT]\n"
        "    [--input-mode auto|tuner|direct_i|direct_q --if-bandwidth Hz (0=automatic)]\n"
        "    Applies settings, checks receiver startup when required, and reads back driver values. No I/Q is recorded.\n"
        "  capture --output NEW_DIR [--frequency Hz --rate Hz --seconds SEC --gain DB_OR_AUTO --device INDEX --settle SEC]\n"
        "  synth --output NEW_DIR [--frequency Hz --rate Hz --seconds SEC --tone Hz --amplitude 0..0.95]\n"
        "  inspect --input capture.sigmf-meta\n"
        "  export --input capture.sigmf-meta --output NEW_DIR\n"
        "  store-snapshot --input EVENT_DIR\n"
        "  replay-events --input capture.sigmf-meta [--speed 0 --bw all --search-width Hz --threshold-db 18]\n"
        "                [--lora 0|1 --fsk 0|1 --ook 0|1 --ask 0|1 --fm 0|1]\n"
        "  detect --input capture.sigmf-meta [--bw Hz --sf-min 7 --sf-max 12 --search-width Hz --threshold-db 18]\n"
        "  scan --input capture.sigmf-meta [--first-sample N --samples N --search-width Hz]\n"
        "       [--modulations all|";
    const auto options=snapshotDetectorOptions();
    for(auto option=options.begin();option!=options.end();++option)
        std::cout<<(option==options.begin()?"":",")<<option->id;
    std::cout<<"]\n"
        "    Emits one JSON line per overlapping analysis window. Sample indices are absolute.\n"
        "  fm-audio --input capture.sigmf-meta --frequency Hz --output NEW_DIR\n"
        "           [--first-sample N --samples N --deemphasis-us 0|50|75 --deviation-hz 50000|75000]\n"
        "  monitor --output EVENT_DIR [--seconds 0 --quota-mib 1024 --max-event-mib 32 --max-event-seconds 8 --keep-hours 0]\n"
        "          [--analysis-queue-mib 16 --analysis-queue-seconds 5 --ring-mib 32 --ring-seconds 8]\n"
        "    Use --input capture.sigmf-meta for real-time replay. Live options match capture. Pre-trigger I/Q is buffered in memory.\n"
        "    --replay-speed 0.05..1 slows input from a recording. Default: 1.\n"
        "    Use --lora 0|1 --fsk 0|1 --ook 0|1 --ask 0|1 --fm 0|1 to select detectors.\n"
        "    For compatibility, monitor --fsk 1 also enables unspecified OOK, ASK and FM detectors.\n"
        "    Use --dense-short-symbols 1 with detect or monitor to check every symbol up to 512 microseconds.\n"
        "    monitor --audit-iq-output NEW_DIR saves up to 8 seconds and 32 MiB of diagnostic I/Q.\n"
        "  replay --input capture.sigmf-meta [--start SEC --duration SEC --speed FACTOR --output NEW_DIR]\n"
        "Replay speed defaults to 1. Use 0 for maximum speed. Press Ctrl+C to stop.\n";
}
}
static int run(int argc,char** argv) {
    std::signal(SIGINT,on_signal); std::signal(SIGTERM,on_signal);
    try {
        if(argc<2 || std::string(argv[1])=="--help") { help(); return 0; }
        const std::string command=argv[1]; Args args(argc,argv);
        if(command=="--build-info") {args.finish();std::cout<<Json({{"version",distribution::applicationVersion()},{"platform",distribution::platformId()},{"receiver_abi",1}}).dump()<<'\n';return 0;}
        if(command=="--version") {args.finish();std::cout<<distribution::applicationVersion()<<'\n';return 0;}
        if(command=="receiver-plugins") {args.finish();std::cout<<receiver_plugin_diagnostics().dump(2)<<'\n';return 0;}
        const bool live_command=command=="devices" || command=="probe" || command=="monitor" || command=="capture" || command=="validate" || command=="configure" || command=="parameters" || command=="recommend";
        const auto& fallback_receiver=default_receiver();
        const auto& receiver=live_command?receiver_by_name(args.get("--receiver",fallback_receiver.id)):fallback_receiver;
        const auto* default_gain=receiver.default_gain;
        if(command=="devices") {
            const auto settings=Json::parse(args.get("--settings","{}"));args.finish();
            if(!settings.is_object())throw std::invalid_argument("--settings requires a JSON object");
            std::cout<<receiverDevices(receiver,settings).dump(2)<<'\n';return 0;
        }
        if(command=="probe") {
            const auto index=args.integer("--device",0,1000);
            const auto serial=args.get("--serial");
            const auto settings=Json::parse(args.get("--settings","{}"));
            if(!settings.is_object())throw std::invalid_argument("--settings requires a JSON object");
            args.finish();
            std::cout<<receiverProbe(receiver,static_cast<std::uint32_t>(index),serial,settings).dump(2)<<'\n';
            return 0;
        }
        if(command=="parameters" || command=="recommend") {
            const auto c=config(args,0,default_gain);args.finish();
            const auto caps=receiverProbe(receiver,c.device,c.device_serial,c.receiver_settings).at("capabilities").get<ReceiverCapabilities>();
            if(command=="parameters")std::cout<<receiverParameters(caps,c).dump(2)<<'\n';
            else {auto list=Json::array();for(const auto& candidate:receiverRecommendations(caps,c))list.push_back(requestedReceiverConfiguration(candidate));std::cout<<list.dump(2)<<'\n';}
            return 0;
        }
        if(command=="configure") {
            const auto c=config(args,0,default_gain);args.finish();
            std::cout<<receiver.configure(c).dump(2)<<'\n';return 0;
        }
        if(command=="validate") {
            const auto c=config(args,0,default_gain);
            const auto purpose=args.get("--purpose","monitor");
            if(purpose!="monitor" && purpose!="capture")throw std::invalid_argument("Use monitor or capture for --purpose");
            LoRaConfig lc;lc.rate=c.rate;lc.receiver_frequency=c.frequency;
            bandwidths(args,lc);lc.search_width=args.real("--search-width",lc.search_width);
            lc.sf_min=static_cast<int>(args.integer("--sf-min",7,12));lc.sf_max=static_cast<int>(args.integer("--sf-max",12,12));
            const bool lora=args.integer("--lora",1,1)!=0;
            const auto narrow=narrowbandFlags(args,true);args.finish();
            const ReceiverCapabilities caps=receiverProbe(receiver,c.device,c.device_serial,c.receiver_settings).at("capabilities");
            const auto plan=purpose=="capture"?planReceiverConfiguration(caps,c):
                planMonitorConfiguration(caps,c,lc,lora,narrow.any());
            std::cout<<Json({{"capabilities",caps},{"requested",requestedReceiverConfiguration(c)},{"plan",plan}}).dump(2)<<'\n';
            return plan.valid()?0:1;
        }
        if(command=="export") {
            const auto input=path_from_utf8(args.required("--input"));
            const auto output=path_from_utf8(args.required("--output"));
            args.finish();
            export_recording(input,output);
            std::cout<<Json({{"status","complete"},{"directory",path_to_utf8(output)}}).dump(2)<<'\n';
            return 0;
        }
        if(command=="store-snapshot") {
            const auto root=path_from_utf8(args.required("--input"));
            args.finish();
            std::cout<<readEventStoreSnapshot(root).dump()<<'\n';
            return 0;
        }
        if(command=="replay-events") {
            const auto info=inspect_recording(path_from_utf8(args.required("--input")));
            ReplaySessionOptions options;
            options.speed=args.real("--speed",0);
            bandwidths(args,options.lora);
            options.lora.search_width=args.real("--search-width",options.lora.search_width);
            options.lora.peak_to_mean_db=args.real("--threshold-db",options.lora.peak_to_mean_db);
            options.enableLora=args.integer("--lora",1,1)!=0;
            options.narrowband=narrowbandFlags(args,false);
            args.finish();
            const auto status=replaySession(info,std::move(options),[]{return false;},cancelled);
            std::cout<<status.dump(2)<<'\n';
            return status.at("state")=="stopped"?130:0;
        }
        if(command=="scan") {
            const auto info=inspect_recording(path_from_utf8(args.required("--input")));
            RecordingScanOptions options;
            options.first=args.integer("--first-sample",0,info.pairs);
            options.count=args.integer("--samples",info.pairs-options.first,info.pairs-options.first);
            options.search_width=args.real("--search-width",std::min(1000000.,double(info.rate)));
            options.detectors=scanSelection(args.get("--modulations","all"));
            args.finish();
            scanRecording(info,options,[](const Json& window){std::cout<<window.dump()<<'\n';},cancelled);
            return cancelled()?130:0;
        }
        if(command=="fm-audio") {
            const auto info=inspect_recording(path_from_utf8(args.required("--input")));
            const auto output=path_from_utf8(args.required("--output"));
            const auto frequency=args.real("--frequency",0);
            if(frequency<=0)throw std::invalid_argument("FM export requires a positive --frequency in Hz");
            const auto first=args.integer("--first-sample",0,info.pairs);
            const auto count=args.integer("--samples",info.pairs-first,info.pairs-first);
            const auto deemphasis=args.real("--deemphasis-us",50);
            const auto deviation=args.real("--deviation-hz",75000);
            args.finish();
            const auto result=exportFmAudio(info,first,count,frequency,deemphasis,deviation,output,cancelled);
            std::cout<<result.dump(2)<<'\n';
            return result.at("status")=="cancelled"?130:0;
        }
        if(command=="monitor") {
            const auto output=path_from_utf8(args.required("--output")),input=path_from_utf8(args.get("--input"));
            const auto replaySpeed=args.real("--replay-speed",1);
            validateMonitorReplaySpeed(!input.empty(),replaySpeed);
            auto c=config(args,0,default_gain);
            auto format=receiver.format;
            if(!input.empty()) {
                const auto info=inspect_recording(input);c.rate=info.rate;format=info.format;
                c.frequency=static_cast<std::uint64_t>(info.frequency); c.seconds=double(info.pairs)/info.rate;
            }
            const auto stride=bytes_per_pair(format);
            LoRaConfig lc; lc.rate=c.rate; lc.receiver_frequency=c.frequency;
            bandwidths(args,lc);
            lc.sf_min=static_cast<int>(args.integer("--sf-min",7,12));
            lc.sf_max=static_cast<int>(args.integer("--sf-max",12,12));
            lc.search_width=args.real("--search-width",1000000);
            lc.peak_to_mean_db=args.real("--threshold-db",18);
            lc.time_phases=static_cast<unsigned>(args.integer("--time-phases",2,8));
            lc.workers=static_cast<unsigned>(args.integer("--workers",lc.workers,32));
            lc.probe_stride_symbols=static_cast<unsigned>(args.integer("--probe-stride",4,4));
            lc.dense_short_symbols=args.integer("--dense-short-symbols",0,1)!=0;
            const auto mode=args.get("--iq-mode","both");
            if(mode!="both" && mode!="normal" && mode!="inverted") throw std::invalid_argument("Use both, normal or inverted for --iq-mode");
            lc.normal_iq=mode!="inverted";lc.inverted_iq=mode!="normal";
            RetentionPolicy policy;
            policy.quota_bytes=args.integer("--quota-mib",1024,65536)*1024*1024;
            policy.max_event_bytes=args.integer("--max-event-mib",32,512)*1024*1024;
            policy.ring_bytes=args.integer("--ring-mib",policy.ring_bytes/1048576,128)*1024*1024;
            policy.reserve_bytes=args.integer("--reserve-mib",256,65536)*1024*1024;
            policy.ring_seconds=args.real("--ring-seconds",policy.ring_seconds);
            policy.pre_seconds=args.real("--pre-seconds",.5);
            policy.post_seconds=args.real("--post-seconds",.5);
            policy.max_event_seconds=args.real("--max-event-seconds",8);
            policy.max_age_hours=args.real("--keep-hours",0);
            policy.max_files=static_cast<std::size_t>(args.integer("--max-files",256,4096));
            policy.diagnostic_candidates=args.integer("--diagnostic",0,1)!=0;
            const auto audit_output=path_from_utf8(args.get("--audit-iq-output"));
            const auto narrowband=narrowbandFlags(args,true);
            const bool lora=args.integer("--lora",1,1)!=0;
            args.finish();
            std::unique_ptr<Recorder> audit;
            MonitorObserver observer;
            observer.narrowband=narrowband;
            observer.enable_lora=lora;
            if(!audit_output.empty()) {
                if(c.seconds<=0 || c.seconds>8)
                    throw std::invalid_argument("Diagnostic I/Q recording requires a finite duration of at most 8 seconds");
                const auto target=sample_count(c.seconds,c.rate);
                if(target>32ULL*1024*1024/stride)throw std::invalid_argument("Diagnostic I/Q recording is limited to 32 MiB");
                const auto event_dir=std::filesystem::weakly_canonical(std::filesystem::absolute(output));
                const auto audit_dir=std::filesystem::weakly_canonical(std::filesystem::absolute(audit_output));
                const auto is_inside=[](const auto& path,const auto& directory) {
                    const auto relative=path.lexically_relative(directory);
                    return !relative.empty() && *relative.begin()!="..";
                };
                if(is_inside(audit_dir,event_dir)||is_inside(event_dir,audit_dir))
                    throw std::invalid_argument("Diagnostic I/Q and event directories must be separate");
                const auto source=input.empty()?std::string("monitor-audit-")+receiver.recording_source:"derived-replay";
                audit=std::make_unique<Recorder>(audit_dir,c,source,format);
                observer.audit_iq=[&audit,target,stride](std::span<const std::uint8_t> block,std::uint64_t first) {
                    if(first!=audit->pairs() || first+block.size()/stride>target)
                        throw std::runtime_error("Diagnostic I/Q samples are discontinuous or exceed the limit");
                    audit->append(block);
                };
            }
            auto result=monitor(c,lc,policy,output,input,cancelled,observer,receiver,replaySpeed);
            if(audit) {
                const auto audit_status=cancelled()?"cancelled":
                    audit->pairs()==sample_count(c.seconds,c.rate)?"complete":"incomplete";
                audit->finish(audit_status,
                    {{"stream_first_sample",0},{"stream_last_sample_exclusive",audit->pairs()},
                     {"source_result",result.at("source_result")}});
                result["audit_iq"]={{"directory",path_to_utf8(audit_output)},{"sample_count",audit->pairs()},{"status",audit_status}};
            }
            std::cout<<result.dump(2)<<'\n'; return cancelled()?130:0;
        }
        if(command=="capture" || command=="synth") {
            const auto output=path_from_utf8(args.required("--output")); auto c=config(args,10,default_gain);
            if(c.seconds<=0) throw std::invalid_argument("Manual capture requires a positive finite duration. Use monitor for continuous operation");
            double tone=0,amplitude=0;
            if(command=="synth") {
                tone=args.real("--tone",200000); amplitude=args.real("--amplitude",0.25);
                if(std::abs(tone)>=c.rate/2.0 || amplitude<0 || amplitude>0.95) throw std::invalid_argument("Invalid test tone offset or amplitude");
            }
            args.finish();
            Recorder recorder(output,c,command=="synth"?"synthetic-tone":receiver.recording_source,
                              command=="synth"?SampleFormat::cu8:receiver.format);
            if(command=="capture") { std::cout<<capture_receiver(receiver,c,recorder,cancelled).dump(2)<<'\n'; return cancelled()?130:0; }
            const auto target=sample_count(c.seconds,c.rate); PowerStats stats; Bytes bytes(c.block_bytes);
            while(recorder.pairs()<target && !cancelled()) {
                const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(bytes.size()/2,target-recorder.pairs()));
                for(std::size_t n=0;n<size;++n) {
                    const double phase=2*std::numbers::pi*tone*double(recorder.pairs()+n)/c.rate;
                    bytes[2*n]=static_cast<std::uint8_t>(std::lround(127.5+128*amplitude*std::cos(phase)));
                    bytes[2*n+1]=static_cast<std::uint8_t>(std::lround(127.5+128*amplitude*std::sin(phase)));
                }
                const auto block=std::span(bytes.data(),size*2); recorder.append(block); stats.add(block);
            }
            const auto status=cancelled()?"cancelled":"complete";
            recorder.finish(status,stats.json()); std::cout<<stats.json().dump(2)<<'\n'; return cancelled()?130:0;
        }
        if(command=="detect") {
            const auto info=inspect_recording(path_from_utf8(args.required("--input")));
            LoRaConfig cfg; cfg.rate=info.rate; cfg.receiver_frequency=info.frequency;
            bandwidths(args,cfg);
            cfg.sf_min=static_cast<int>(args.integer("--sf-min",7,12));
            cfg.sf_max=static_cast<int>(args.integer("--sf-max",12,12));
            cfg.search_width=args.real("--search-width",1000000);
            cfg.peak_to_mean_db=args.real("--threshold-db",18);
            cfg.time_phases=static_cast<unsigned>(args.integer("--time-phases",2,8));
            cfg.workers=static_cast<unsigned>(args.integer("--workers",cfg.workers,32));
            cfg.probe_stride_symbols=static_cast<unsigned>(args.integer("--probe-stride",4,4));
            cfg.dense_short_symbols=args.integer("--dense-short-symbols",0,1)!=0;
            const auto mode=args.get("--iq-mode","both");
            if(mode!="both" && mode!="normal" && mode!="inverted") throw std::invalid_argument("Use both, normal or inverted for --iq-mode");
            cfg.normal_iq=mode!="inverted";cfg.inverted_iq=mode!="normal";
            args.finish(); WidebandLoRa detector(cfg,info.format);
            const auto started=std::chrono::steady_clock::now();
            replay(info,0,info.pairs,0,131072*bytes_per_pair(info.format),
                   [&](auto bytes,auto first){ detector.feed(bytes,first); },cancelled);
            detector.finish();auto result=detector.results(); result["source"]="recorded";
            result["cancelled"]=cancelled();
            result["processing_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            std::cout<<result.dump(2)<<'\n'; return cancelled()?130:0;
        }
        if(command=="inspect" || command=="replay") {
            const auto info=inspect_recording(path_from_utf8(args.required("--input")));
            if(command=="inspect") {
                args.finish(); std::cout<<Json({{"metadata",info.metadata},{"pairs",info.pairs},{"duration_seconds",double(info.pairs)/info.rate}}).dump(2)<<'\n'; return 0;
            }
            const auto start=args.real("--start",0), duration=args.real("--duration",-1), speed=args.real("--speed",1);
            const auto output=path_from_utf8(args.get("--output")); args.finish();
            if(start<0 || start>double(info.pairs)/info.rate || (duration!=-1 && duration<=0)) throw std::invalid_argument("Invalid replay range");
            const auto first=static_cast<std::uint64_t>(std::floor(start*info.rate));
            const auto count=duration==-1 ? info.pairs-first : sample_count(duration,info.rate);
            if(count>info.pairs-first) throw std::invalid_argument("Replay range exceeds the recording");
            std::unique_ptr<Recorder> recorder;
            if(!output.empty()) {
                Config c; c.rate=info.rate; c.frequency=static_cast<std::uint64_t>(info.frequency);
                c.seconds=double(std::max<std::uint64_t>(count,1))/info.rate;
                const auto& global=info.metadata.at("global");
                c.auto_gain=global.value("signalscope:gain_mode",std::string("auto"))=="auto";
                c.gain_tenths=global.value("signalscope:gain_tenths_db",0);
                recorder=std::make_unique<Recorder>(output,c,"derived-replay",info.format);
            }
            PowerStats stats(info.format);
            replay(info,first,count,speed,65536,[&](auto bytes,auto){ stats.add(bytes); if(recorder) recorder->append(bytes); },cancelled);
            Json result={{"source","recorded"},{"first_sample",first},{"requested_pairs",count},{"power",stats.json()},
                {"original_status",info.metadata["global"].value("signalscope:status","external")},{"speed",speed}};
            if(recorder) recorder->finish(cancelled()?"cancelled":"complete",{{"parent",path_to_utf8(std::filesystem::absolute(info.data_path))},
                {"parent_first_sample",first},{"note","Derived recording; timestamps are replay observation, not original RF time"},{"analysis",result}});
            std::cout<<result.dump(2)<<'\n'; return cancelled()?130:0;
        }
        throw std::invalid_argument("Unknown command: "+command);
    } catch(const std::exception& e) { std::cerr<<"Error: "<<e.what()<<'\n'; return 1; }
}

#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    std::vector<std::string> utf8;
    utf8.reserve(argc);
    for(int i=0;i<argc;++i)utf8.push_back(path_to_utf8(argv[i]));
    std::vector<char*> arguments;
    arguments.reserve(utf8.size());
    for(auto& value:utf8)arguments.push_back(value.data());
    return run(argc,arguments.data());
}
#else
int main(int argc,char** argv) { return run(argc,argv); }
#endif
