#include "signalscope/adapters/receivers/plugin_support.hpp"
#include "profile.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <limits>
#include <thread>
#ifdef SIGNALSCOPE_HAS_RTLSDR
#include <rtl-sdr.h>
#endif

namespace signalscope {
namespace {
#ifdef SIGNALSCOPE_HAS_RTLSDR
void check(int code,const char* operation,ErrorCode category=ErrorCode::receiver_configuration) {
    if (code<0) throw OperationError(category,std::string(operation)+" failed with driver error "+std::to_string(code));
}
struct DeviceClose { void operator()(rtlsdr_dev_t* d) const { if(d) rtlsdr_close(d); } };
struct Context {
    rtlsdr_dev_t* device;
    BoundedQueue<Bytes>& queue;
    std::uint64_t discard, target, accepted=0,rejected=0;
    std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::int64_t> last_callback{0};
    std::string error;
    Context(rtlsdr_dev_t* d,BoundedQueue<Bytes>& q,std::uint64_t skip,std::uint64_t count)
        : device(d),queue(q),discard(skip),target(count) {}
    static std::int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void fail(const char* text) { error=text; failed=true; rtlsdr_cancel_async(device); }
    static void receive(unsigned char* data,std::uint32_t length,void* opaque) noexcept {
        auto& s=*static_cast<Context*>(opaque);
        try {
            ++s.callbacks; s.last_callback=now_ms();
            if (s.failed) return;
            if (length%2) { s.fail("Driver returned an incomplete I/Q pair"); return; }
            const auto drop=std::min<std::uint64_t>(s.discard,length/2);
            s.discard-=drop; data+=drop*2; length-=static_cast<std::uint32_t>(drop*2);
            const auto pairs=std::min<std::uint64_t>(length/2,s.target-s.accepted);
            if (pairs && !s.queue.try_push(Bytes(data,data+pairs*2))) {
                s.rejected+=pairs;
                s.fail("Capture stopped because the recording queue filled. The tail was not saved"); return;
            }
            s.accepted+=pairs;
            if (s.accepted==s.target) rtlsdr_cancel_async(s.device);
        } catch (...) { s.failed=true; rtlsdr_cancel_async(s.device); }
    }
};
Json list_devices() {
    Json result=Json::array();
    for (std::uint32_t n=0;n<rtlsdr_get_device_count();++n) {
        char maker[256]{},product[256]{},serial[256]{};
        const auto status=rtlsdr_get_device_usb_strings(n,maker,product,serial);
        result.push_back({{"index",n},{"name",rtlsdr_get_device_name(n)},{"manufacturer",maker},
            {"product",product},{"serial",serial},{"usb_strings_status",status}});
    }
    return {{"backend","librtlsdr"},{"devices",result}};
}
ReceiverCapabilities capabilities(rtlsdr_dev_t* raw,const std::vector<int>& gains) {
    char maker[256]{},product[256]{},serial[256]{};
    rtlsdr_get_usb_strings(raw,maker,product,serial);
    return rtlReceiverCapabilities(static_cast<int>(rtlsdr_get_tuner_type(raw)),gains,maker,product);
}
Json probe_device(std::uint32_t index,std::string_view serial) {
    if(!serial.empty())throw std::invalid_argument("Use --device for RTL-SDR");
    rtlsdr_dev_t* raw=nullptr; check(rtlsdr_open(&raw,index),"Open RTL-SDR",ErrorCode::receiver_open);
    std::unique_ptr<rtlsdr_dev_t,DeviceClose> device(raw);
    const auto n=rtlsdr_get_tuner_gains(raw,nullptr);
    if(n<0) throw std::runtime_error("Could not query tuner gains");
    std::vector<int> gains(static_cast<std::size_t>(n));
    if(n && rtlsdr_get_tuner_gains(raw,gains.data())!=n) throw std::runtime_error("Tuner gain list changed during enumeration");
    const auto tuner=static_cast<int>(rtlsdr_get_tuner_type(raw));
    const auto caps=capabilities(raw,gains);
    return {{"device",index},{"tuner_type",tuner},{"hardware",caps.model},
        {"gain_tenths_db",gains},{"capabilities",caps},
        {"note","Tuner frequency ranges are nominal profiles, not measured RF passbands"}};
}
Json applyDevice(rtlsdr_dev_t* raw,const Config& cfg) {
    const auto n=rtlsdr_get_tuner_gains(raw,nullptr);
    if(n<0)throw OperationError(ErrorCode::receiver_configuration,"Could not query tuner gains");
    std::vector<int> gains(static_cast<std::size_t>(n));
    if(n && rtlsdr_get_tuner_gains(raw,gains.data())!=n)
        throw OperationError(ErrorCode::receiver_configuration,"Tuner gain list changed during enumeration");
    const auto caps=capabilities(raw,gains);
    requireReceiverConfiguration(receiver_plugin::validateHardware(caps,cfg));
    const auto mode=receiverInputMode(caps,cfg);
    const int direct=mode=="direct_i"?1:mode=="direct_q"?2:0;
    check(rtlsdr_set_direct_sampling(raw,direct),"Set receiver input mode");
    if(rtlsdr_get_direct_sampling(raw)!=direct)
        throw OperationError(ErrorCode::receiver_configuration,"RTL-SDR did not apply the requested input mode");
    check(rtlsdr_set_center_freq(raw,cfg.frequency),"Tune receiver");
    check(rtlsdr_set_sample_rate(raw,cfg.rate),"Set sample rate");
    if(!direct) {
        check(rtlsdr_set_tuner_gain_mode(raw,cfg.auto_gain?0:1),"Set gain mode");
        if(!cfg.auto_gain)check(rtlsdr_set_tuner_gain(raw,cfg.gain_tenths),"Set manual gain");
    }
    const auto rate=rtlsdr_get_sample_rate(raw),frequency=rtlsdr_get_center_freq(raw);
    const auto gain=rtlsdr_get_tuner_gain(raw);
    if(rate!=cfg.rate || frequency!=cfg.frequency || (!direct && !cfg.auto_gain && gain!=cfg.gain_tenths))
        throw OperationError(ErrorCode::receiver_configuration,"Driver-reported configuration differs from the request");
    return {{"requested",requestedReceiverConfiguration(cfg)},
        {"actual",{{"sample_rate_hz",rate},{"frequency_hz",frequency},
                   {"gain_tenths_db",direct || cfg.auto_gain?Json():Json(gain)},{"sample_datatype","cu8"},{"input_mode",mode},
                   {"filter_bandwidth_hz",nullptr},{"antenna",nullptr},{"auto_gain",nullptr}}},
        {"readback_source","driver"},{"gain_mode_confirmation",direct?"not_applicable_direct_sampling":"driver_acknowledged"}};
}
Json configure(const Config& cfg) {
    cfg.validate();
    if(!cfg.device_serial.empty())throw std::invalid_argument("Use --device for RTL-SDR");
    rtlsdr_dev_t* raw=nullptr;check(rtlsdr_open(&raw,cfg.device),"Open RTL-SDR",ErrorCode::receiver_open);
    std::unique_ptr<rtlsdr_dev_t,DeviceClose> device(raw);
    return applyDevice(raw,cfg);
}
Json stream_rtlsdr(const Config& cfg, const BlockSink& sink,const std::function<bool()>& cancelled,const ReceiverReady& ready) {
    if(!cfg.device_serial.empty())throw std::invalid_argument("Use --device for RTL-SDR");
    cfg.validate();
    if(!cfg.antenna.empty())
        throw OperationError(ErrorCode::receiver_configuration,"RTL-SDR does not expose selectable antenna ports");
    rtlsdr_dev_t* raw=nullptr; check(rtlsdr_open(&raw,cfg.device),"Open RTL-SDR",ErrorCode::receiver_open);
    std::unique_ptr<rtlsdr_dev_t,DeviceClose> device(raw);
    const auto applied=applyDevice(raw,cfg);
    const auto actual_rate=rtlsdr_get_sample_rate(raw), actual_frequency=rtlsdr_get_center_freq(raw);
    if (actual_rate!=cfg.rate || actual_frequency!=cfg.frequency)
        throw OperationError(ErrorCode::receiver_configuration,"Driver-reported sample rate or frequency differs from the request");
    check(rtlsdr_reset_buffer(raw),"Reset USB buffer");
    auto conditions=AcquisitionConditions{}.json();conditions["origin"]="provider";
    conditions["adc_overload"]["capability"]="unavailable";
    conditions["adc_overload"]["evidence"]={{"reason_code","not_exposed_by_provider"}};
    if(ready)ready({double(actual_rate),double(actual_frequency),0,applied,AcquisitionConditions(std::move(conditions))});
    BoundedQueue<Bytes> queue(cfg.queue_blocks);
    const auto target=cfg.seconds>0?sample_count(cfg.seconds,cfg.rate):std::numeric_limits<std::uint64_t>::max()/2;
    Context context{raw,queue,static_cast<std::uint64_t>(cfg.settle_seconds*cfg.rate),target};
    context.last_callback=Context::now_ms();
    std::atomic<bool> done=false, timed_out=false;
    std::atomic<int> driver_result=0;
    const auto started=std::chrono::steady_clock::now();
    std::jthread acquisition([&]{
        driver_result=rtlsdr_read_async(raw,&Context::receive,&context,0,static_cast<std::uint32_t>(cfg.block_bytes));
        done=true; queue.close();
    });
    std::jthread watchdog([&](std::stop_token stop){
        while(!stop.stop_requested() && !done) {
            const bool stalled=Context::now_ms()-context.last_callback.load()>3000;
            const bool over_budget=cfg.seconds>0 && std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>cfg.seconds+cfg.settle_seconds+5;
            if (cancelled() || stalled || over_budget || context.failed) {
                if (stalled || over_budget) timed_out=true;
                rtlsdr_cancel_async(raw);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    PowerStats power;double max_sink_seconds=0;
    try {
        Bytes block;
        while(queue.pop(block)) {
            const auto before_sink=std::chrono::steady_clock::now();
            sink(block,power.pairs);
            max_sink_seconds=std::max(max_sink_seconds,std::chrono::duration<double>(std::chrono::steady_clock::now()-before_sink).count());
            power.add(block);
        }
    } catch (...) {
        rtlsdr_cancel_async(raw); acquisition.join(); watchdog.request_stop(); watchdog.join(); throw;
    }
    acquisition.join(); watchdog.request_stop(); watchdog.join();
    Json result={{"backend","librtlsdr"},{"requested_rate_hz",cfg.rate},{"actual_rate_hz",actual_rate},
        {"actual_frequency_hz",actual_frequency},{"actual_gain_tenths_db",rtlsdr_get_tuner_gain(raw)},{"configuration",applied},
        {"callbacks",context.callbacks.load()},{"queue_high_water_blocks",queue.high_water()},
        {"queue_capacity_blocks",cfg.queue_blocks},{"queue_capacity_bytes",cfg.queue_blocks*cfg.block_bytes},
        {"accepted_pairs",context.accepted},{"rejected_pairs",context.rejected},{"max_sink_seconds",max_sink_seconds},
        {"driver_result",driver_result.load()},{"watchdog_timeout",timed_out.load()},
        {"elapsed_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()},
        {"power",power.json()},{"continuity","USB samples have no hardware sequence counter; exact lost sample count unavailable"}};
    const bool complete=power.pairs==context.target;
    if (context.failed || driver_result<0 || timed_out || (!complete && !cancelled())) {
        result["error"]=context.error.empty()?"Driver, timeout or callback failure":context.error;
        result["status"]="error";
        throw SourceError(result["error"].get<std::string>(),result);
    }
    result["status"]=complete?"complete":"cancelled";
    return result;
}
#else
Json list_devices() {
    return {{"backend","unavailable"},{"devices",Json::array()},{"reason_code",receiver_backend_not_built},
            {"reason","Built without librtlsdr. Offline analysis remains available"}};
}
Json probe_device(std::uint32_t,std::string_view) { throw OperationError(ErrorCode::receiver_unavailable,"Built without the librtlsdr backend"); }
Json configure(const Config&) {throw OperationError(ErrorCode::receiver_unavailable,"Built without the librtlsdr backend");}
Json stream_rtlsdr(const Config&,const BlockSink&,const std::function<bool()>&,const ReceiverReady&) {
    throw OperationError(ErrorCode::receiver_unavailable,"Built without librtlsdr. Install its development package and rebuild");
}
#endif
}
ReceiverPlan devicePlan(const ReceiverCapabilities& caps,const Config& cfg) {
    auto p=receiver_plugin::validateHardware(caps,cfg);
    p.nominal_adc_bits=8;
    if(cfg.rate>2560000)p.issues.push_back({"rtl_usb_rate_risk","sample_rate_hz","information",false});
    return p;
}
const ReceiverBackend& rtlsdr_receiver() {
    static constexpr int gains[]{197,280,386,496};
    static const ReceiverBackend receiver{"live-rtlsdr","rtlsdr",list_devices,probe_device,stream_rtlsdr,
                                          SampleFormat::cu8,"19.7","rtlsdr","RTL-SDR",gains,ReceiverGainKind::tuner,
                                          100'000,1'740'000'000,default_monitor_rates,configure,devicePlan,receiver_plugin::recommendHardware,receiver_plugin::describeParameters};
    return receiver;
}
}
