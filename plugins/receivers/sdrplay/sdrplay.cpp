#include "signalscope/adapters/receivers/plugin_support.hpp"

#ifdef SIGNALSCOPE_HAS_SOAPY
#include "service.hpp"
#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.hpp>
#include <SoapySDR/Formats.hpp>

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#endif

namespace signalscope {
namespace {
constexpr auto driver="sdrplay";
constexpr auto backend_name="soapy-sdrplay";

#ifdef SIGNALSCOPE_HAS_SOAPY
constexpr auto rx=SOAPY_SDR_RX;
constexpr std::size_t channel=0;

struct CloseDevice {
    void operator()(SoapySDR::Device* device) const noexcept {
        delete device;
    }
};
using Device=std::unique_ptr<SoapySDR::Device,CloseDevice>;

Device open_device(std::uint32_t& index,std::string_view serial,const Json& settings=Json::object()) try {
    const auto devices=sdrplay_service::enumerate(settings);
    if(!serial.empty()) {
        std::optional<std::size_t> matched;
        for(std::size_t i=0;i<devices.size();++i) {
            const auto found=devices[i].find("serial");
            if(found==devices[i].end() || found->second!=serial)continue;
            if(matched)throw OperationError(ErrorCode::receiver_open,"Multiple SDRplay entries match this serial. Select a device index.");
            matched=i;
        }
        if(!matched)throw OperationError(ErrorCode::receiver_open,"The selected SDRplay serial was not found");
        index=static_cast<std::uint32_t>(*matched);
    }
    if(index>=devices.size())throw OperationError(ErrorCode::receiver_open,"SDRplay device index not found");
    Device device(sdrplay_service::open(devices[index]));
    if(!device)throw OperationError(ErrorCode::receiver_open,"Could not open SDRplay device");
    return device;
} catch(const OperationError&) {
    throw;
} catch(const std::exception& error) {
    throw OperationError(ErrorCode::receiver_open,error.what());
}

Json connected_devices(const Json& settings) {
    Json result=Json::array();
    for(const auto& device:sdrplay_service::enumerate(settings))
        result.push_back({{"index",result.size()},{"identification",device}});
    if(result.empty())return {{"backend","unavailable"},{"devices",result},{"reason_code","service_unavailable"},
        {"message_key","connection.unavailable"},{"endpoint",sdrplay_service::endpoint(settings)}};
    return {{"backend",backend_name},{"devices",result},{"transport","soapy-remote"},{"endpoint",sdrplay_service::endpoint(settings)}};
}
Json devices() {return connected_devices(Json::object());}

bool supports_if_reduction(const SoapySDR::Range& range,double gain) {
    if(!std::isfinite(gain) || !std::isfinite(range.minimum()) || !std::isfinite(range.maximum()) ||
       !std::isfinite(range.step()) || range.step()<0 ||
       gain<range.minimum() || gain>range.maximum() || std::trunc(gain)!=gain)return false;
    if(range.step()==0)return true;
    return std::abs(std::remainder(gain-range.minimum(),range.step()))<1e-6;
}

Json typedSetting(const std::string& value,const SoapySDR::ArgInfo& info) {
    if(info.type==SoapySDR::ArgInfo::BOOL)return value=="true" || value=="1";
    try {
        if(info.type==SoapySDR::ArgInfo::INT)return Json(std::stoll(value));
        if(info.type==SoapySDR::ArgInfo::FLOAT)return Json(std::stod(value));
    }catch(const std::exception&) {}
    return value;
}
Json settingDescriptors(SoapySDR::Device& device) {
    auto result=Json::array();
    for(const auto& info:device.getSettingInfo()) {
        // Mode changes can destroy/recreate a multi-tuner device; this adapter
        // exposes single-stream RX controls only, not RSPduo topology changes.
        if(info.key=="rspduo_mode" || info.key=="rspduo_sample_freq")continue;
        std::string value=info.value;try {value=device.readSetting(info.key);}catch(const std::exception&) {}
        const auto type=info.type==SoapySDR::ArgInfo::BOOL?"boolean":!info.options.empty()?"enum":
            info.type==SoapySDR::ArgInfo::INT?"integer":info.type==SoapySDR::ArgInfo::FLOAT?"number":"string";
        Json d={{"key",info.key},{"type",type},{"label_key","setting."+info.key+".label"},{"label",info.name.empty()?info.key:info.name},
            {"description_key","setting."+info.key+".description"},{"description",info.description},{"default",typedSetting(value,info)},
            {"requires_stop",true},{"source","driver"}};
        if(!info.options.empty()) {d["choices"]=Json::array();for(const auto& option:info.options)d["choices"].push_back(typedSetting(option,info));}
        if(info.range.maximum()>info.range.minimum()) {d["minimum"]=info.range.minimum();d["maximum"]=info.range.maximum();
            if(info.range.step()>0)d["step"]=info.range.step();}
        if(!info.units.empty())d["unit"]=info.units;
        if(info.key=="hdr_ctrl") {
            d["enabled_frequency_max_hz"]=2000000;d["default"]=false;

        }
        if(info.key=="agc_setpoint"){d["requires_auto_gain"]=true;d["inactive_allowed"]=true;}
        result.push_back(std::move(d));
    }
    return result;
}
ReceiverCapabilities capabilities(SoapySDR::Device& device) {
    ReceiverCapabilities c;c.backend="sdrplay";c.model=device.getHardwareKey();c.format=SampleFormat::ci16_le;
    c.frequency_source="driver";c.rate_source="driver";
    for(const auto& r:device.getFrequencyRange(rx,channel))c.frequency_ranges.push_back({r.minimum(),r.maximum(),r.step()});
    if(c.frequency_ranges.empty() && (c.model=="RSPdx" || c.model=="RSPdx-R2")) {
        c.frequency_ranges={{1000,2e9}};c.frequency_source="hardware_specification";
    }
    for(const auto value:device.listSampleRates(rx,channel))
        if(std::isfinite(value) && value>=1 && value<=100e6 && std::floor(value)==value)c.sample_rates.push_back(static_cast<std::uint32_t>(value));
    for(const auto& r:device.getSampleRateRange(rx,channel))c.sample_rate_ranges.push_back({r.minimum(),r.maximum(),r.step()});
    c.recommended_rates={10000000,8000000,6000000,2000000};
    c.extensions["gain_semantic"]="if_reduction";c.extensions["settings"]=settingDescriptors(device);
    for(const auto& parameter:sdrplay_service::parameters())c.extensions["settings"].push_back(parameter);
    c.filter_bandwidths=device.listBandwidths(rx,channel);c.antennas=device.listAntennas(rx,channel);
    const auto gain=device.getGainRange(rx,channel,"IFGR");c.manual_gains_known=true;
    for(int db=-100;db<=100;++db)if(supports_if_reduction(gain,db))c.manual_gains_tenths.push_back(db*10);
    const bool profiled=c.model=="RSPdx" || c.model=="RSPdx-R2";
    if(profiled){c.limited_antenna="Antenna C";c.limited_antenna_max_hz=200e6;}
    return c;
}

Json connected_probe(std::uint32_t index,std::string_view serial,const Json& settings) {
    auto device=open_device(index,serial,settings);
    double full_scale=0;
    const auto native=device->getNativeStreamFormat(rx,channel,full_scale);
    const auto gain=device->getGainRange(rx,channel,"IFGR");
    std::vector<int> gains;
    for(int db=-100;db<=100;++db)
        if(supports_if_reduction(gain,db))gains.push_back(db*10);
    auto information=Json::array();
    const auto hardware=device->getHardwareInfo();
    if(const auto version=hardware.find("sdrplay_api_api_version");version!=hardware.end())
        information.push_back({{"label_key","information.hardware_api.label"},{"value",version->second}});
    return {{"backend",backend_name},{"device",index},{"hardware",device->getHardwareKey()},{"information",information},
        {"hardware_info",device->getHardwareInfo()},{"bandwidths_hz",device->listBandwidths(rx,channel)},
        {"antennas",device->listAntennas(rx,channel)},{"sample_rates_hz",device->listSampleRates(rx,channel)},
        {"native_format",native},{"native_full_scale",full_scale},{"gain_tenths_db",gains},
        {"if_gain_reduction_db",{{"min",gain.minimum()},{"max",gain.maximum()},{"step",gain.step()}}},
        {"capabilities",capabilities(*device)},{"transport","soapy-remote"},{"endpoint",sdrplay_service::endpoint(settings)}};
}
Json probe(std::uint32_t index,std::string_view serial) {return connected_probe(index,serial,Json::object());}

class Stream {
public:
    Stream(SoapySDR::Device& device,SoapySDR::Stream* stream):device_(device),stream_(stream){}
    ~Stream() {
        if(!stream_)return;
        if(active_)try {device_.deactivateStream(stream_);} catch(...) {}
        try {device_.closeStream(stream_);} catch(...) {}
    }
    Stream(const Stream&)=delete;
    Stream& operator=(const Stream&)=delete;
    void start() try {
        if(!stream_ || device_.activateStream(stream_)!=0)
            throw OperationError(ErrorCode::receiver_stream,"Could not activate the SDRplay stream");
        active_=true;
    } catch(const OperationError&) {
        throw;
    } catch(const std::exception& error) {
        throw OperationError(ErrorCode::receiver_stream,error.what());
    }
    SoapySDR::Stream* get() const {return stream_;}
private:
    SoapySDR::Device& device_;
    SoapySDR::Stream* stream_;
    bool active_=false;
};

std::size_t transportWindow(const Config& cfg) {
    return std::clamp<std::size_t>(std::size_t(double(cfg.rate)*bytes_per_pair(SampleFormat::ci16_le)*.2),256*1024,8*1024*1024);
}
SoapySDR::Stream* setup(SoapySDR::Device& device,const Config& cfg) try {
    // The pinned upstream frame adds 24 header bytes and budgets 48 network
    // header bytes even for TCP. Limit payload independently from socket queues.
    const auto payload=std::min<std::size_t>(cfg.block_bytes,16*1024);
    return device.setupStream(rx,SOAPY_SDR_CS16,{channel},{{"remote:prot","tcp"},{"remote:format",SOAPY_SDR_CS16},
        {"remote:mtu",std::to_string(payload+72)},{"remote:window",std::to_string(transportWindow(cfg))},{"remote:priority","0"}});
} catch(const std::exception& error) {
    throw OperationError(ErrorCode::receiver_configuration,error.what());
}

Json applyDevice(SoapySDR::Device& device,const Config& cfg) {
    cfg.validate();
    requireReceiverConfiguration(receiver_plugin::validateHardware(capabilities(device),cfg));
    std::string hardware,antenna;
    double actual_rate=0,actual_frequency=0,actual_bandwidth=0,actual_if_gain=0;
    bool auto_gain=false;
    try {
        hardware=device.getHardwareKey();
        antenna=cfg.antenna.empty()?device.getAntenna(rx,channel):cfg.antenna;
        if((hardware=="RSPdx-R2" || hardware=="RSPdx") && antenna=="Antenna C" && cfg.frequency>200'000'000)
            throw OperationError(ErrorCode::receiver_antenna_frequency,
                                 "SDRplay Antenna C supports frequencies up to 200 MHz");
        double full_scale=0;
        if(device.getNativeStreamFormat(rx,channel,full_scale)!=SOAPY_SDR_CS16 ||
           (full_scale!=32767 && full_scale!=32768))
            throw OperationError(ErrorCode::receiver_configuration,"Expected full-scale native CS16 from SDRplay");
        device.setSampleRate(rx,channel,cfg.rate);
        actual_rate=device.getSampleRate(rx,channel);
        if(!(std::abs(actual_rate-cfg.rate)<=1))
            throw OperationError(ErrorCode::receiver_configuration,"SDRplay did not apply the requested sample rate");
        const auto bandwidth=receiver_plugin::validateHardware(capabilities(device),cfg).filter_bandwidth_hz;
        if(bandwidth>0)device.setBandwidth(rx,channel,bandwidth);
        actual_bandwidth=device.getBandwidth(rx,channel);
        if(bandwidth>0 && !(std::abs(actual_bandwidth-bandwidth)<=1))
            throw OperationError(ErrorCode::receiver_configuration,"SDRplay did not apply the requested IF bandwidth");
        if(cfg.required_passband_hz>actual_rate+1)
            throw OperationError(ErrorCode::receiver_passband,"SDRplay sampled span "+std::to_string(actual_rate)+
                                 " Hz is narrower than the required passband "+std::to_string(cfg.required_passband_hz)+" Hz");
        device.setFrequency(rx,channel,cfg.frequency);
        if(!cfg.antenna.empty()) {
            const auto antennas=device.listAntennas(rx,channel);
            if(std::find(antennas.begin(),antennas.end(),cfg.antenna)==antennas.end())
                throw OperationError(ErrorCode::receiver_configuration,"Unknown SDRplay antenna port: "+cfg.antenna);
            device.setAntenna(rx,channel,cfg.antenna);
            if(device.getAntenna(rx,channel)!=cfg.antenna)
                throw OperationError(ErrorCode::receiver_configuration,"SDRplay did not apply the requested antenna");
        }
        device.setGainMode(rx,channel,cfg.auto_gain);
        auto_gain=device.getGainMode(rx,channel);
        if(auto_gain!=cfg.auto_gain)
            throw OperationError(ErrorCode::receiver_configuration,"SDRplay did not apply the requested gain mode");
        if(!cfg.auto_gain) {
            const double gain=cfg.gain_tenths/10.;
            const auto range=device.getGainRange(rx,channel,"IFGR");
            if(!supports_if_reduction(range,gain))
                throw OperationError(ErrorCode::receiver_configuration,"Unsupported SDRplay IF gain reduction. Use probe to list supported values.");
            device.setGain(rx,channel,"IFGR",gain);
            actual_if_gain=device.getGain(rx,channel,"IFGR");
            if(!(std::abs(actual_if_gain-gain)<.01))
                throw OperationError(ErrorCode::receiver_configuration,"SDRplay did not apply the requested IF gain reduction");
        }
        actual_frequency=device.getFrequency(rx,channel);
        if(!(std::abs(actual_frequency-cfg.frequency)<=1))
            throw OperationError(ErrorCode::receiver_configuration,"SDRplay did not apply the requested frequency");
        antenna=device.getAntenna(rx,channel);
    } catch(const OperationError&) {
        throw;
    } catch(const std::exception& error) {
        throw OperationError(ErrorCode::receiver_configuration,error.what());
    }
    Json settings=Json::object();
    const auto information=device.getSettingInfo();
    for(const auto& [key,value]:cfg.receiver_settings.items()) {
        if(key=="service_endpoint") {settings[key]=sdrplay_service::endpoint(cfg.receiver_settings);continue;}
        const auto info=std::find_if(information.begin(),information.end(),[&](const auto& item){return item.key==key;});
        if(info==information.end())throw OperationError(ErrorCode::receiver_configuration,"Unreported SDRplay setting: "+key);
        device.writeSetting(key,value.is_string()?value.get<std::string>():value.dump());
        const auto actual=typedSetting(device.readSetting(key),*info);
        if(actual!=value)throw OperationError(ErrorCode::receiver_configuration,"SDRplay setting readback differs from request: "+key);
        settings[key]=actual;
    }
    return {{"requested",requestedReceiverConfiguration(cfg)},
        {"actual",{{"sample_rate_hz",actual_rate},{"frequency_hz",actual_frequency},
                   {"filter_bandwidth_hz",actual_bandwidth},{"antenna",antenna},
                   {"auto_gain",auto_gain},{"gain_tenths_db",auto_gain?Json():Json(actual_if_gain*10)},
                   {"sample_datatype","ci16_le"},{"settings",settings}}},
        {"readback_source","driver"}};
}
Json confirmActivated(SoapySDR::Device& device,const Config& cfg,Json applied) {
    // SoapySDRPlay defers hardware initialization until stream activation.
    // Do not report a successful apply based only on pre-activation parameters.
    auto& actual=applied.at("actual");
    const double rate=device.getSampleRate(rx,channel),frequency=device.getFrequency(rx,channel),
        bandwidth=device.getBandwidth(rx,channel);
    if(!(std::abs(rate-cfg.rate)<=1) || !(std::abs(frequency-cfg.frequency)<=1) ||
       !(std::abs(bandwidth-actual.at("filter_bandwidth_hz").get<double>())<=1) ||
       device.getGainMode(rx,channel)!=cfg.auto_gain || device.getAntenna(rx,channel)!=actual.at("antenna").get<std::string>())
        throw OperationError(ErrorCode::receiver_configuration,"SDRplay configuration changed during stream activation");
    if(!cfg.auto_gain && !(std::abs(device.getGain(rx,channel,"IFGR")*10-cfg.gain_tenths)<=.1))
        throw OperationError(ErrorCode::receiver_configuration,"SDRplay gain changed during stream activation");
    actual["sample_rate_hz"]=rate;actual["frequency_hz"]=frequency;actual["filter_bandwidth_hz"]=bandwidth;
    applied["activation_confirmed"]=true;return applied;
}
Json configure(const Config& cfg) {
    cfg.validate();
    auto index=cfg.device;auto device=open_device(index,cfg.device_serial,cfg.receiver_settings);
    auto applied=applyDevice(*device,cfg);
    Stream input(*device,setup(*device,cfg));input.start();
    return confirmActivated(*device,cfg,std::move(applied));
}
Json stream(const Config& cfg,const BlockSink& sink,const std::function<bool()>& cancelled,const ReceiverReady& ready) {
    cfg.validate();
    auto index=cfg.device;
    auto device=open_device(index,cfg.device_serial,cfg.receiver_settings);
    auto applied=applyDevice(*device,cfg);
    Stream input(*device,setup(*device,cfg));input.start();
    applied=confirmActivated(*device,cfg,std::move(applied));
    const auto& actual=applied.at("actual");
    const double actual_rate=actual.at("sample_rate_hz"),actual_frequency=actual.at("frequency_hz"),
        actual_bandwidth=actual.at("filter_bandwidth_hz");
    const std::string hardware=device->getHardwareKey(),antenna=actual.at("antenna");
    const bool auto_gain=actual.at("auto_gain");
    const double actual_if_gain=auto_gain?0:actual.at("gain_tenths_db").get<double>()/10;
    auto conditions=AcquisitionConditions{}.json();conditions["origin"]="provider";
    conditions["adc_overload"]["capability"]="unavailable";
    conditions["adc_overload"]["evidence"]={{"reason_code","not_exposed_by_provider"}};
    if(ready)ready({actual_rate,actual_frequency,actual_bandwidth,applied,AcquisitionConditions(std::move(conditions))});
    const auto stride=bytes_per_pair(SampleFormat::ci16_le);
    std::vector<std::int16_t> samples(cfg.block_bytes/sizeof(std::int16_t));
    const auto block_pairs=cfg.block_bytes/stride;
    std::size_t buffered=0;
    const auto target=cfg.seconds>0?sample_count(cfg.seconds,cfg.rate):std::numeric_limits<std::uint64_t>::max();
    auto settle=static_cast<std::uint64_t>(cfg.settle_seconds*cfg.rate);
    auto last_data=std::chrono::steady_clock::now();
    PowerStats power(SampleFormat::ci16_le);
    std::uint64_t reads=0,short_reads=0,timeouts=0,minimum_read=0,maximum_read=0,delivered_blocks=0;
    auto* bytes=reinterpret_cast<std::uint8_t*>(samples.data());
    const auto flush=[&] {
        if(!buffered)return;
        if constexpr(std::endian::native==std::endian::big)
            for(std::size_t i=0;i<buffered*stride;i+=2)std::swap(bytes[i],bytes[i+1]);
        const std::span<const std::uint8_t> block(bytes,buffered*stride);
        sink(block,power.pairs);
        power.add(block);
        ++delivered_blocks;
        buffered=0;
    };
    Json report={{"backend",backend_name},{"device",index},{"requested_rate_hz",cfg.rate},
        {"transport","soapy-remote"},{"service_endpoint",sdrplay_service::endpoint(cfg.receiver_settings)},
        {"transport_budget",{{"requested_socket_window_bytes",transportWindow(cfg)},{"maximum_payload_bytes",std::min<std::size_t>(cfg.block_bytes,16*1024)},
                             {"hardware_sample_loss_count",nullptr},{"server_driver_read_counts",nullptr}}},
        {"hardware",hardware},{"actual_rate_hz",actual_rate},{"actual_frequency_hz",actual_frequency},
        {"actual_bandwidth_hz",actual_bandwidth},{"antenna",antenna},{"auto_gain",auto_gain},
        {"continuity","SoapySDR discontinuity flags; no hardware sample counter exposed"},{"configuration",applied}};
    if(!auto_gain)report["actual_if_gain_reduction_db"]=actual_if_gain;
    if(!cfg.device_serial.empty())report["device_serial"]=cfg.device_serial;
    if(cfg.required_passband_hz>0)report["required_passband_hz"]=cfg.required_passband_hz;
    const auto publish_read_stats=[&] {
        report["read_stats"]={{"scope","client_transport_reads"},{"reads",reads},{"short_reads",short_reads},{"timeouts",timeouts},
            {"min_read_pairs",minimum_read},{"max_read_pairs",maximum_read},{"delivered_blocks",delivered_blocks}};
    };
    while(power.pairs<target && !cancelled()) {
        const auto request=std::min<std::uint64_t>(block_pairs-buffered,target-power.pairs-buffered);
        void* buffers[]={samples.data()+buffered*2};
        int flags=0;
        long long time_ns=0;
        int count=0;
        try {
            count=device->readStream(input.get(),buffers,request,flags,time_ns,100000);
        } catch(const std::exception& error) {
            flush();
            publish_read_stats();
            report["status"]="error";
            report["accepted_pairs"]=power.pairs;
            report["error"]=error.what();
            throw SourceError(error.what(),report);
        }
        if(count<=0 && cancelled())break;
        if(count==SOAPY_SDR_TIMEOUT || count==0) {
            ++timeouts;
            if(std::chrono::steady_clock::now()-last_data<std::chrono::seconds(3))continue;
        } else if(count>0 && std::uint64_t(count)<=request && !(flags&SOAPY_SDR_END_ABRUPT)) {
            const auto pairs=static_cast<std::uint64_t>(count);
            minimum_read=reads?std::min(minimum_read,pairs):pairs;
            maximum_read=std::max(maximum_read,pairs);
            ++reads;
            if(pairs<request)++short_reads;
            last_data=std::chrono::steady_clock::now();
            const auto skip=std::min<std::uint64_t>(settle,count);
            settle-=skip;
            if(skip==std::uint64_t(count))continue;
            if(skip)std::memmove(bytes+buffered*stride,bytes+(buffered+skip)*stride,(count-skip)*stride);
            buffered+=count-skip;
            if(buffered==block_pairs || buffered==target-power.pairs)flush();
            continue;
        }
        flush();
        publish_read_stats();
        report["status"]="error";
        report["accepted_pairs"]=power.pairs;
        report["driver_result"]=count;
        report["stream_flags"]=flags;
        const bool abrupt=(flags&SOAPY_SDR_END_ABRUPT)!=0;
        const bool discontinuity=abrupt || count==SOAPY_SDR_OVERFLOW;
        if(discontinuity)report["reason_code"]="stream_discontinuity";
        const auto* error=abrupt?"SDRplay stream ended abruptly":
            count<0?SoapySDR::errToStr(count):"SDRplay stream stopped or returned invalid data";
        report["error"]=error;
        throw SourceError(error,report);
    }
    flush();
    publish_read_stats();
    report["status"]=power.pairs==target?"complete":"cancelled";
    report["accepted_pairs"]=power.pairs;
    report["rejected_pairs"]=0;
    report["power"]=power.json();
    return report;
}
#else
Json devices() {
    return {{"backend","unavailable"},{"devices",Json::array()},{"reason_code",receiver_backend_not_built},
            {"reason","Built without SoapySDR"}};
}
Json probe(std::uint32_t,std::string_view) {
    throw OperationError(ErrorCode::receiver_unavailable,"Built without SoapySDR");
}
Json configure(const Config&) {
    throw OperationError(ErrorCode::receiver_unavailable,"Built without SoapySDR");
}
Json stream(const Config&,const BlockSink&,const std::function<bool()>&,const ReceiverReady&) {
    throw OperationError(ErrorCode::receiver_unavailable,"Built without SoapySDR");
}
#endif
}

ReceiverPlan devicePlan(const ReceiverCapabilities& caps,const Config& cfg) {
    auto p=receiver_plugin::validateHardware(caps,cfg);
    if((caps.model=="RSPdx" || caps.model=="RSPdx-R2") && cfg.rate>=2e6 && cfg.rate<=10.66e6)
        p.nominal_adc_bits=cfg.rate<=6048000?14:cfg.rate<=8064000?12:cfg.rate<=9216000?10:8;
    return p;
}
const ReceiverBackend& sdrplay_receiver() {
    static constexpr std::uint32_t rates[]{2'000'000,2'048'000,3'000'000};
    static const ReceiverBackend receiver=[] {
        ReceiverBackend result{"live-sdrplay",backend_name,devices,probe,stream,
                                          SampleFormat::ci16_le,"auto","sdrplay","SDRplay",{},ReceiverGainKind::if_reduction,
                                          1'000,2'000'000'000,rates,configure,devicePlan,receiver_plugin::recommendHardware,receiver_plugin::describeParameters};
#ifdef SIGNALSCOPE_HAS_SOAPY
        result.connection_parameters=sdrplay_service::parameters();
        result.connected_devices=connected_devices;result.connected_probe=connected_probe;
#endif
        return result;
    }();
    return receiver;
}
}
