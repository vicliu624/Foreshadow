#include "signalscope/backend/sessions/monitor.hpp"
#include "signalscope/backend/iq/reception.hpp"
#include "signalscope/backend/sessions/receiver_configuration.hpp"
#include "signalscope/adapters/modulation/wideband_lora.hpp"
#include "signalscope/backend/sessions/wideband_narrowband.hpp"
#include "signalscope/backend/dsp/analysis_priority.hpp"
#include "signalscope/adapters/modulation/fsk.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "process_metrics.hpp"
#include <algorithm>
#include <exception>
#include <thread>
#include <optional>
#ifdef __linux__
#include <sched.h>
#endif

namespace signalscope {
namespace {
struct Block { std::uint64_t first; Bytes bytes; };
struct MonitorWorkers {
    BoundedQueue<Block>& analysis_input;
    BoundedQueue<Block>& recording_input;
    BoundedQueue<Block>& narrowband_input;
    std::jthread analysis,recording,narrowband;

    ~MonitorWorkers(){finish();}

    void finish() {
        // Analysis can still publish results for the recording worker to drain.
        analysis_input.close();
        if(analysis.joinable())analysis.join();
        recording_input.close();
        if(recording.joinable())recording.join();
        narrowband_input.close();
        if(narrowband.joinable())narrowband.join();
    }
};
void shift_event(Json& event,std::uint64_t origin) {
    for(const auto* key:{"first_observed_sample","confirmed_at_sample","sfd_start_sample","sfd_end_sample"})
        if(event.contains(key)) event[key]=event[key].get<std::uint64_t>()+origin;
}
}
void validateMonitorReplaySpeed(bool from_recording,double replay_speed) {
    if(!std::isfinite(replay_speed) || replay_speed<.05 || replay_speed>1 ||
       (!from_recording && replay_speed!=1))
        throw std::invalid_argument("Monitor replay speed must be 0.05–1 and requires a recording");
}

Json monitor(const Config& cfg,const LoRaConfig& lc,const RetentionPolicy& retention,const std::filesystem::path& output,
             const std::filesystem::path& input,const std::function<bool()>& cancelled,const MonitorObserver& observer,
             const ReceiverBackend& receiver,double replay_speed) {
    cfg.validate();
    validateMonitorReplaySpeed(!input.empty(),replay_speed);
    const auto* source=input.empty()?receiver.source:"recorded";
    if(lc.rate!=cfg.rate || lc.receiver_frequency!=cfg.frequency) throw std::invalid_argument("Detector and acquisition settings do not match");
    const auto recording=input.empty()?std::optional<ReplayInfo>{}:inspect_recording(input);
    const auto format=recording?recording->format:receiver.format;
    if(!recording && receiver.probe) {
        const ReceiverCapabilities caps=receiverProbe(receiver,cfg.device,cfg.device_serial,cfg.receiver_settings).at("capabilities");
        requireMonitorConfiguration(planMonitorConfiguration(caps,cfg,lc,observer.enable_lora,
                                    observer.narrowband.any(),!observer.analysis_iq.empty()));
    }
    const IqStreamParameters stream_parameters{
        recording?recording->rate:cfg.rate,recording?recording->frequency:double(cfg.frequency),format};
    IqPublisher capture_iq(observer.capture_iq,stream_parameters);
    const auto stride=bytes_per_pair(format);
    auto detector=observer.enable_lora?std::make_unique<WidebandLoRa>(lc,format):nullptr;
    const bool run_narrowband=observer.narrowband.any();
    const bool run_auxiliary=run_narrowband || !observer.analysis_iq.empty();
    const auto analysis_limit=std::min<std::uint64_t>(cfg.analysis_queue_bytes,sample_count(cfg.analysis_queue_seconds,cfg.rate)*stride);
    if(analysis_limit<cfg.block_bytes)throw std::invalid_argument("The analysis buffer must hold at least one capture block");
    const auto analysis_capacity=static_cast<std::size_t>(analysis_limit/cfg.block_bytes);
    EventStore store(output,cfg,retention,format);
    const auto weight=[](const Block& block){return block.bytes.size();};
    BoundedQueue<Block> queue(analysis_limit,weight);
    const auto recording_capacity=std::max<std::size_t>(1,(32ULL*1024*1024)/cfg.block_bytes);
    BoundedQueue<Block> recording_queue(recording_capacity*cfg.block_bytes,weight);
    BoundedQueue<Json> result_queue(16);std::mutex store_mutex;
    std::mutex status_mutex;std::optional<Json> pending_status;
    std::mutex configuration_mutex;Json applied_configuration;
    std::atomic<std::uint64_t> dropped_pairs=0,source_pairs=0,dropped_results=0,diagnostic_events_not_recorded=0;
    std::atomic<std::uint64_t> recorded_pairs=0;
    std::atomic<std::uint64_t> fsk_skipped=0, fsk_scanned=0, analysis_position=0;
    std::atomic<std::uint64_t> fsk_skipped_pairs=0;
    std::atomic<std::uint64_t> fsk_processed_pairs=0;
    std::atomic<bool> fsk_failed=false;
    // Auxiliary DSP needs the same burst budget as the primary analysis.
    // Account real payload bytes so USB short reads do not shrink that budget.
    BoundedQueue<Block> fsk_queue(analysis_limit,weight);
    std::mutex fsk_mutex;
    Json narrowband_status=Json::object();
    const auto run_fsk=[&]{
        lowerAnalysisPriority();
        try {
            auto fsk=run_narrowband?std::make_unique<WidebandNarrowband>(cfg,lc.search_width,format,observer.narrowband):nullptr;
            NarrowbandEvents narrowbandEvents;
            auto last_snapshot=std::chrono::steady_clock::now();
            const auto snapshot=[&]{
                Json status; narrowbandEvents.publish(status);
                if(fsk)status["narrowband_analysis"]=fsk->status();
                std::lock_guard lock(fsk_mutex);narrowband_status=std::move(status);
            };
            IqPublisher analysis_iq(observer.analysis_iq,stream_parameters);
            Block block;std::optional<Block> deferred;
            for(;;) {
                if(deferred){block=std::move(*deferred);deferred.reset();}
                else if(!fsk_queue.pop(block))break;
                ++fsk_scanned;
                Block next;
                while(block.bytes.size()+cfg.block_bytes<=262144 && fsk_queue.try_pop(next)) {
                    if(next.first!=block.first+block.bytes.size()/stride){deferred=std::move(next);break;}
                    block.bytes.insert(block.bytes.end(),next.bytes.begin(),next.bytes.end());
                    ++fsk_scanned;
                }
                analysis_iq.publish(block.bytes,block.first);
                if(fsk)fsk->feed(block.bytes,block.first,narrowbandEvents);
                fsk_processed_pairs+=block.bytes.size()/stride;
                if(std::chrono::steady_clock::now()-last_snapshot>=std::chrono::milliseconds(250)) {
                    snapshot();last_snapshot=std::chrono::steady_clock::now();
                }
            }
            if(fsk)fsk->finish(narrowbandEvents);
            snapshot();
        } catch(...) {fsk_failed=true;}
    };
    auto publish_fsk=[&](Json& status){
        if(!run_auxiliary)return;
        std::lock_guard lock(fsk_mutex);
        for(const auto& [key,value]:narrowband_status.items())status[key]=value;
        status["fsk_analysis"]={{"scanned_blocks",fsk_scanned.load()},{"skipped_blocks",fsk_skipped.load()},
            {"skipped_pairs",fsk_skipped_pairs.load()},
            {"processed_pairs",fsk_processed_pairs.load()},{"queue_high_water_blocks",fsk_queue.high_water()},
            {"queue_capacity_bytes",analysis_limit},{"queue_high_water_bytes",fsk_queue.high_water_weight()},
            {"queue_capacity_seconds",double(analysis_limit)/(stride*cfg.rate)},
            {"unavailable",fsk_failed.load()}};
    };
    std::atomic<std::uint64_t> dropped_result_events=0;
    std::atomic<bool> analysis_failed=false,writer_failed=false;
    std::exception_ptr failure,writer_failure;
    Json latest=Json::array(),candidates=Json::array(),last_status=Json::object(),source_result;
    std::string failure_text;
    const auto started=std::chrono::steady_clock::now();
    Spectrometer spectrometer(format);
    std::mutex transient_mutex;Json transient;
    std::vector<float> spectral_baseline;unsigned baseline_frames=0;
    double power_baseline=0;
    const auto run_analysis=[&]{
        lowerAnalysisPriority();
        std::uint64_t expected=0,origin=0,processed=0,gaps=0;
        auto last_report=std::chrono::steady_clock::now()-std::chrono::seconds(5);
        auto last_disk=last_report;
        std::optional<double> last_cpu;
        std::uint64_t observed_peak_rss=0;
        auto last_load_time=started;
        const auto processors=std::max(1u,std::thread::hardware_concurrency());
        auto publish=[&](const std::string& state,bool persist=true){
            Json storage;bool storage_stale=false;
            {std::unique_lock lock(store_mutex,std::try_to_lock);
             if(lock.owns_lock())storage=store.status();
             else {storage=last_status.value("storage",Json::object());storage_stale=true;}}
            const auto captured=source_pairs.load(),recorded=recorded_pairs.load();
            last_status={{"state",state},{"source",source},
                {"source_input",path_to_utf8(input)},{"sample_datatype",sample_datatype(format)},{"source_pairs",captured},{"processed_pairs",processed},
                {"analysis_dropped_pairs",dropped_pairs.load()},{"analysis_gaps",gaps},
                {"analysis_queue_high_water_blocks",queue.high_water()},{"result_dropped_groups",dropped_results.load()},{"capture_pipeline","independent_capture_writer_v3"},
                {"analysis_queue_capacity_bytes",analysis_capacity*cfg.block_bytes},
                {"analysis_queue_high_water_bytes",queue.high_water_weight()},
                {"analysis_queue_capacity_seconds",double(analysis_capacity*cfg.block_bytes)/(stride*cfg.rate)},
                {"analysis_inflight_seconds",double(std::max<std::size_t>(cfg.block_bytes,256*1024))/(stride*cfg.rate)},
                {"source_queue_capacity_bytes",cfg.queue_blocks*cfg.block_bytes},{"buffering_policy","bounded_burst_v1"},
                {"recording_processed_pairs",recorded},{"recording_queue_high_water_blocks",recording_queue.high_water()},
                {"recording_queue_capacity_bytes",recording_capacity*cfg.block_bytes},{"storage_status_stale",storage_stale},
                {"recording_lag_seconds",double(captured>recorded?captured-recorded:0)/cfg.rate},
                {"result_queue_policy","recordable_events_only"},{"diagnostic_events_not_recorded",diagnostic_events_not_recorded.load()},
                {"result_dropped_events",dropped_result_events.load()},
                {"analysis_lag_seconds",double(source_pairs.load()>expected?source_pairs.load()-expected:0)/cfg.rate},
                {"elapsed_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()},
                {"updated_utc",utc_now()},{"storage",storage},{"recent_lora_events",latest},{"recent_candidates",candidates},
                {"receiver_configuration",{{"frequency_hz",cfg.frequency},{"sample_rate_hz",cfg.rate},{"gain_tenths_db",cfg.gain_tenths},{"auto_gain",cfg.auto_gain},{"search_width_hz",lc.search_width}}},
                {"detector",{{"algorithm","dechirp-fft-structure-v3.1"},{"bandwidth_hz",lc.bandwidth},
                    {"preamble_threshold_db",lc.peak_to_mean_db},{"downchirp_threshold_db",lc.peak_to_mean_db-2},
                    {"sf_min",lc.sf_min},{"sf_max",lc.sf_max},{"time_phases",lc.time_phases},{"workers",lc.workers},
                    {"probe_stride_symbols",lc.probe_stride_symbols},{"dense_short_symbols",lc.dense_short_symbols},
                    {"normal_iq",lc.normal_iq},{"inverted_iq",lc.inverted_iq},
                    {"bandwidth_candidates_hz",lc.bandwidth_candidates.empty()?std::vector<double>{lc.bandwidth}:lc.bandwidth_candidates},
                    {"scores_calibrated",false}}}};
            if(recording)last_status["replay_speed"]=replay_speed;
            if(!recording) {
                std::lock_guard lock(configuration_mutex);
                last_status["receiver_configuration"]["applied"]=applied_configuration;
            }
            const auto metrics=readProcessMetrics();
            if(metrics.cpu_seconds) {
                const auto now=std::chrono::steady_clock::now();
                const auto elapsed=std::chrono::duration<double>(now-last_load_time).count();
                const auto cores=last_cpu && elapsed>0?std::max(0.,*metrics.cpu_seconds-*last_cpu)/elapsed:0;
                last_status["cpu_percent_of_machine"]=100*cores/processors;
                last_status["cpu_cores_used"]=cores;
                last_status["logical_processors"]=processors;
                last_cpu=metrics.cpu_seconds;
                last_load_time=now;
            }
            if(metrics.resident_bytes) {
                last_status["rss_bytes"]=*metrics.resident_bytes;
                observed_peak_rss=std::max(observed_peak_rss,*metrics.resident_bytes);
            }
            if(metrics.peak_resident_bytes) {
                // Some kernels account for the current and peak values separately.
                observed_peak_rss=std::max(observed_peak_rss,*metrics.peak_resident_bytes);
                last_status["peak_rss_bytes"]=observed_peak_rss;
            }
            publish_fsk(last_status);
            if(detector)last_status["wideband_analysis"]=detector->status();
            if(!failure_text.empty()) last_status["error"]=failure_text;
            if(persist) {{std::lock_guard lock(status_mutex);pending_status=last_status;}last_disk=std::chrono::steady_clock::now();}
            if(observer.status) observer.status(last_status);
        };
        try {
            publish("starting");
            Block block;std::optional<Block> deferred;
            for(;;) {
                if(deferred){block=std::move(*deferred);deferred.reset();}
                else if(!queue.pop(block))break;
                // Bound dispatch overhead at high rates to roughly one batch per
                // 5 ms. Capture and recording never wait for this DSP batching.
                // A gap or end of source flushes the contiguous partial batch.
                Block next;
                const auto target=std::clamp<std::size_t>(std::size_t(cfg.rate/200)*stride,cfg.block_bytes,std::max<std::size_t>(cfg.block_bytes,262144));
                while(block.bytes.size()+cfg.block_bytes<=target && queue.pop(next)) {
                    if(next.first!=block.first+block.bytes.size()/stride){deferred=std::move(next);break;}
                    block.bytes.insert(block.bytes.end(),next.bytes.begin(),next.bytes.end());
                }
                if(block.first!=expected) {
                    ++gaps; origin=block.first;
                    if(detector)detector->reset();
                }
                Json events=Json::array();
                if(detector) {
                    detector->feed(block.bytes,block.first-origin);
                    events=detector->take_events();
                }
                if(retention.diagnostic_candidates) {
                    std::lock_guard lock(transient_mutex);
                    if(!transient.is_null() && transient["confirmed_at_sample"].get<std::uint64_t>()<=block.first+block.bytes.size()/stride) {
                        auto event=std::move(transient);transient=nullptr;
                        // 已经是全局样本位置；下面统一加 origin 前先转为当前局部索引。
                        if(event["first_observed_sample"].get<std::uint64_t>()>=origin) {
                            event["first_observed_sample"]=event["first_observed_sample"].get<std::uint64_t>()-origin;
                            event["confirmed_at_sample"]=event["confirmed_at_sample"].get<std::uint64_t>()-origin;
                            events.push_back(std::move(event));
                        }
                    }
                }
                Json record_events=Json::array();
                for(auto& event:events) {
                    shift_event(event,origin);
                    event["source"]=source;
                    if(event.value("evidence_level",std::string())=="lora_structure_match") {
                        latest.push_back(event); if(latest.size()>32) latest.erase(latest.begin());
                    } else if(event.value("evidence_level",std::string())=="chirp_probe" || event.value("evidence_level",std::string())=="energy_transient") {
                        candidates.push_back(event);if(candidates.size()>8)candidates.erase(candidates.begin());
                    }
                    // 常规模式的探针只用于状态显示，不能堵住录制触发队列。
                    if(retention.diagnostic_candidates || event.value("evidence_level",std::string())=="lora_structure_match")
                        record_events.push_back(std::move(event));
                    else ++diagnostic_events_not_recorded;
                }
                if(!record_events.empty()&&!result_queue.try_push(std::move(record_events)))++dropped_results;
                processed+=block.bytes.size()/stride; expected=block.first+block.bytes.size()/stride;
                if(run_auxiliary)analysis_position.store(expected,std::memory_order_relaxed);
                if(observer.analysis_progress)observer.analysis_progress(expected);
                if(std::chrono::steady_clock::now()-last_report>=std::chrono::seconds(1)) {
                    publish("listening",std::chrono::steady_clock::now()-last_disk>=std::chrono::seconds(5)); last_report=std::chrono::steady_clock::now();
                }
            }
            if(detector) {
                detector->finish();auto tail=detector->take_events();Json recordable=Json::array();
                for(auto& event:tail) {
                    shift_event(event,origin);event["source"]=source;
                    if(event.value("evidence_level",std::string())=="lora_structure_match") {
                        latest.push_back(event);if(latest.size()>32)latest.erase(latest.begin());recordable.push_back(std::move(event));
                    }
                }
                if(!recordable.empty() && !result_queue.try_push(std::move(recordable)))++dropped_results;
            }
            publish("analysis_finished");
        } catch(const std::exception& e) {
            failure=std::current_exception(); failure_text=e.what(); analysis_failed=true;
            try {publish("error");} catch(...) {}
        }
    };
    std::exception_ptr source_failure;
    ReceiverReadback receiver_readback{double(cfg.rate),double(cfg.frequency)};
    auto drain_results=[&]{
        Json events=Json::array(),group;
        for(int n=0;n<16&&result_queue.try_pop(group);++n)
            for(auto& event:group){if(events.size()<4096)events.push_back(std::move(event));else ++dropped_result_events;}
        return events;
    };
    const auto run_writer=[&]{
        Json future=Json::array();
        auto eligible=[&](std::uint64_t end){
            for(auto& event:drain_results()) {
                if(future.size()<4096)future.push_back(std::move(event));else ++dropped_result_events;
            }
            Json ready=Json::array(),later=Json::array();
            for(auto& event:future) {
                if(event.at("confirmed_at_sample").get<std::uint64_t>()<=end)ready.push_back(std::move(event));
                else later.push_back(std::move(event));
            }
            future=std::move(later);return ready;
        };
        auto persist_status=[&]{
            std::optional<Json> status;
            {std::lock_guard lock(status_mutex);status=std::move(pending_status);pending_status.reset();}
            if(status)write_json(output/"monitor-status.json",*status);
        };
        try {
            Block block;
            while(recording_queue.pop(block)) {
                const auto end=block.first+block.bytes.size()/stride;
                if(observer.audit_iq)observer.audit_iq(block.bytes,block.first);
                auto events=eligible(end);
                {std::lock_guard lock(store_mutex);store.feed(block.bytes,block.first,events);}
                recorded_pairs=end;
                if(observer.recording_progress)observer.recording_progress(end);
                persist_status();
            }
            // 主线程先等待分析结束，再关闭录制队列，故此处可收齐末尾结果。
            auto events=eligible(recorded_pairs.load());
            {std::lock_guard lock(store_mutex);store.feed({},recorded_pairs.load(),events);}
            dropped_result_events+=future.size();
            persist_status();
        } catch(...) {writer_failure=std::current_exception();writer_failed=true;}
    };
    // The owner must outlive worker startup and be destroyed before captured state.
    MonitorWorkers workers{queue,recording_queue,fsk_queue,{},{},{}};
    if(run_auxiliary)workers.narrowband=std::jthread(run_fsk);
    workers.analysis=std::jthread(run_analysis);
    workers.recording=std::jthread(run_writer);
    auto last_spectrum=started-std::chrono::seconds(1);
    try {
        const auto sink=[&](std::span<const std::uint8_t> bytes,std::uint64_t first){
            if(writer_failed)throw OperationError(ErrorCode::recording_write,"Capture stopped because the recording worker failed");
            if(first!=source_pairs.load() || bytes.empty() || bytes.size()%stride || bytes.size()>cfg.block_bytes)
                throw OperationError(ErrorCode::receiver_stream,"Input I/Q is discontinuous or has an invalid block length");
            source_pairs=first+bytes.size()/stride;
            if((observer.spectrum || retention.diagnostic_candidates) && std::chrono::steady_clock::now()-last_spectrum>=std::chrono::milliseconds(250)) {
                auto frame=spectrometer.measure(bytes,first,
                    static_cast<std::uint32_t>(std::llround(receiver_readback.sample_rate_hz)),
                    receiver_readback.center_frequency_hz);
                frame.search_width_hz=lc.search_width;
                frame.filter_bandwidth_hz=receiver_readback.filter_bandwidth_hz;
                if(observer.spectrum)observer.spectrum(frame);
                if(retention.diagnostic_candidates && !frame.bins_dbfs.empty()) {
                    if(spectral_baseline.empty()) {
                        spectral_baseline.resize(frame.bins_dbfs.size());
                        for(std::size_t i=0;i<frame.bins_dbfs.size();++i)spectral_baseline[i]=static_cast<float>(std::pow(10.0,frame.bins_dbfs[i]/10.0));
                        power_baseline=frame.power_dbfs;
                    }
                    double maximum_rise=-100;std::size_t maximum_bin=0;unsigned adjacent=0;bool narrow_rise=false;
                    for(std::size_t i=0;i<frame.bins_dbfs.size();++i) {
                        const double linear=std::pow(10.0,frame.bins_dbfs[i]/10.0);
                        const double rise=10*std::log10(linear/std::max<double>(spectral_baseline[i],1e-14));
                        if(rise>maximum_rise){maximum_rise=rise;maximum_bin=i;}
                        adjacent=rise>12?adjacent+1:0;if(adjacent>=3)narrow_rise=true;
                        const auto alpha=baseline_frames<8?1.0/double(baseline_frames+1):.02;
                        spectral_baseline[i]=static_cast<float>((1-alpha)*spectral_baseline[i]+alpha*linear);
                    }
                    if(baseline_frames>=8 && (frame.power_dbfs-power_baseline>3 || narrow_rise)) {
                        std::lock_guard lock(transient_mutex);
                        transient={{"evidence_level","energy_transient"},{"first_observed_sample",first},{"confirmed_at_sample",first+std::min<std::size_t>(1024,bytes.size()/stride)},
                            {"spectrum_peak_frequency_hz",cfg.frequency-cfg.rate/2.0+maximum_bin*double(cfg.rate)/frame.bins_dbfs.size()},
                            {"energy_rise_db",std::max(maximum_rise,frame.power_dbfs-power_baseline)},
                            {"note","Bounded diagnostic observation of an energy change"}};
                    }
                    power_baseline=.98*power_baseline+.02*frame.power_dbfs;++baseline_frames;
                }
                last_spectrum=std::chrono::steady_clock::now();
            }
            // 写盘、淘汰和元数据均在独立线程；满队列明确停止，绝不覆盖原始样本。
            if(!recording_queue.try_push({first,Bytes(bytes.begin(),bytes.end())}))
                throw OperationError(ErrorCode::recording_write,"Capture stopped because the recording buffer filled. The original tail is incomplete");
            if(!queue.try_push({first,Bytes(bytes.begin(),bytes.end())})) dropped_pairs+=bytes.size()/stride;
            capture_iq.publish(bytes,first);
            if(run_auxiliary && !fsk_failed.load()) {
                if(!fsk_queue.try_push({first,Bytes(bytes.begin(),bytes.end())})){++fsk_skipped;fsk_skipped_pairs+=bytes.size()/stride;}
            }
        };
        const auto stop=[&]{return cancelled() || analysis_failed.load() || writer_failed.load();};
        if(!recording) {
            auto capture_config=cfg;
            if(observer.enable_lora || run_auxiliary) {
                capture_config.required_passband_hz=std::max(cfg.required_passband_hz,lc.search_width);
            }
            source_result=receiver.stream(capture_config,sink,stop,[&](const ReceiverReadback& actual){
                receiver_readback=actual;
                std::lock_guard lock(configuration_mutex);
                applied_configuration=actual.configuration;
                applied_configuration["acquisition_conditions"]=actual.conditions;
            });
        }
        else {
            replay(*recording,0,recording->pairs,replay_speed,cfg.block_bytes,sink,stop);
            source_result={{"backend","recorded"},{"pairs",source_pairs.load()}};
        }
    } catch(const SourceError& e) {source_result=e.details;source_failure=std::current_exception();}
      catch(const ReceptionError& e) {source_result=e.status.diagnostic;source_failure=std::current_exception();}
      catch(...) {source_failure=std::current_exception();}
    workers.finish();
    publish_fsk(last_status);
    if(writer_failure)source_failure=writer_failure;
    try {
        store.finish(failure?"analysis_error":source_failure?"source_error":cancelled()?"monitor_cancelled":"monitor_stopped");
        last_status["storage"]=store.status();
        last_status["state"]=(failure||source_failure)?"error":"stopped";
        last_status["result_dropped_groups"]=dropped_results.load();
        last_status["result_dropped_events"]=dropped_result_events.load();
        last_status["recording_processed_pairs"]=recorded_pairs.load();
        last_status["recording_lag_seconds"]=double(source_pairs.load()>recorded_pairs.load()?source_pairs.load()-recorded_pairs.load():0)/cfg.rate;
        last_status["recording_queue_high_water_blocks"]=recording_queue.high_water();
        last_status["storage_status_stale"]=false;
    } catch(...) {if(!source_failure)source_failure=std::current_exception();}
    last_status["source_result"]=source_result;
    if(failure){write_json(output/"monitor-status.json",last_status);if(observer.status)observer.status(last_status);std::rethrow_exception(failure);}
    if(source_failure) {
        last_status["state"]="source_error";
        try {std::rethrow_exception(source_failure);} catch(const std::exception& e) {last_status["error"]=e.what();}
        write_json(output/"monitor-status.json",last_status);if(observer.status)observer.status(last_status); std::rethrow_exception(source_failure);
    }
    last_status["source_result"]=source_result;
    write_json(output/"monitor-status.json",last_status);if(observer.status)observer.status(last_status); return last_status;
}
}
