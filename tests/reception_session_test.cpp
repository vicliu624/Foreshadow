#include "signalscope/backend/sessions/reception.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
using namespace signalscope;
namespace {
void require(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
template<class F>void wait(F predicate,const char* detail){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error(detail);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
struct Observation {
    std::mutex mutex;ReceptionStatus latest;std::atomic<std::uint64_t> pairs=0;std::atomic<unsigned> ready=0;
    ReceptionConsumer consumer(){return {[this](const auto& s){std::lock_guard lock(mutex);latest=s;if(s.stream)++ready;},[this](const auto& b){pairs+=b.bytes.size()/bytes_per_pair(b.stream.format);}};}
    std::string state(){std::lock_guard lock(mutex);return latest.state;}
    unsigned rate(){std::lock_guard lock(mutex);return latest.stream?latest.stream->sample_rate:0;}
};
struct IndependentReceiver {
    std::atomic<unsigned> opens=0,active=0,maximum=0;std::atomic_bool narrowReadback=false;std::atomic<std::uint64_t> failAfter=0;
    ReceiverBackend backend{};ReceiverCapabilities caps;std::chrono::microseconds blockPause{200};
    IndependentReceiver() {
        caps.backend="contract-independent";caps.model="independent input";caps.format=SampleFormat::ci16_le;caps.sample_rates={250000,1000000,2000000,8000000,10000000};
        backend.id="contract-independent";backend.source="independent";backend.format=caps.format;
        backend.probe=[this](auto,auto){require(active==0,"Host probed an already open device");return Json{{"capabilities",caps}};};
        backend.plan=[](const auto&,const Config& cfg){ReceiverPlan p;p.maximum_search_width_hz=cfg.rate;p.filter_bandwidth_hz=cfg.rate;if(cfg.rate<cfg.required_passband_hz)p.issues.push_back({"span","rate","hardware",true});return p;};
        backend.recommend=[this](const auto&,const Config& base){std::vector<Config> choices;for(auto rate:caps.sample_rates){auto c=base;c.rate=rate;choices.push_back(c);}return choices;};
        backend.stream=[this](const Config& cfg,const BlockSink& sink,const auto& stop,const ReceiverReady& ready){
            ++opens;const auto count=++active;maximum=std::max(maximum.load(),count);
            struct End{std::atomic<unsigned>& active;~End(){--active;}}end{active};
            ready({double(narrowReadback?100000:cfg.rate),double(cfg.frequency),double(cfg.rate),{{"sample_rate_hz",cfg.rate}}});
            Bytes bytes(std::min<std::size_t>(cfg.block_bytes,4096));std::uint64_t first=0;
            while(!stop()){if(failAfter && first>=failAfter)throw SourceError("independent_source_failure",{{"reason_code","stream_discontinuity"},{"driver_marker","independent-provider"},{"read_stats",{{"reads",5}}}});sink(bytes,first);first+=bytes.size()/4;std::this_thread::sleep_for(blockPause);}
            return Json{{"source_pairs",first},{"read_stats",{{"reads",first/1024}}}};
        };
    }
};
void sharingAndLifetime() {
    IndependentReceiver device;device.blockPause=std::chrono::milliseconds(20);Config base;base.frequency=100000000;base.rate=10000000;
    ReceiverSession session(device.backend,base);require(session.describe().at("capabilities").at("model")=="independent input","Receiver facts were not preserved");
    Observation monitor,fm,other,broken;auto wide=session.acquire({100000000,10000000,10000000,10000000,base},monitor.consumer());
    wait([&]{return monitor.pairs>10000;},"Independent exact monitor did not start");
    auto listening=session.acquire({100000000,220000,225001,100000000,{}},fm.consumer());wait([&]{return fm.pairs>10000;},"Covered playback did not share capture");
    require(device.opens==1 && device.maximum==1,"Shared receiver opened twice");require(fm.rate()==10000000,"Shared playback lost the actual stream rate");
    auto conflicting=session.acquire({600000000,8000000,8000000,100000000,{}},other.consumer());wait([&]{return other.state()=="conflict";},"Incompatible task did not report conflict");
    auto bad=broken.consumer();bad.offer=[](const auto&){throw std::runtime_error("independent_consumer_failure");};auto failed=session.acquire({100000000,220000,225001,100000000,{}},bad);
    wait([&]{return broken.state()=="consumer_error";},"Consumer failure was not isolated");const auto before=fm.pairs.load();
    wide.reset();wait([&]{return fm.ready>=2 && fm.rate()==250000;},"Playback did not survive monitor release / use a smaller plugin recommendation");
    // This fake source deliberately sleeps between blocks; its throughput is
    // subject to the host timer resolution (notably Windows sleep_for). Test
    // continued delivery here, without imposing an unrelated sample-rate SLA.
    wait([&]{return fm.pairs>before+10000;},"Sustained playback stopped after monitor release");require(device.maximum==1,"Replanning overlapped hardware opens");
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<until){const auto previous=fm.pairs.load();std::this_thread::sleep_for(std::chrono::milliseconds(100));wait([&]{return fm.pairs>previous;},"Continuous generated input stalled after replan");}
    listening.reset();wait([&]{return device.active==0 && session.snapshot().value("state",std::string())=="idle";},"Last playback lease did not release receiver");
    session.disconnect();
}
void readbackAndMonitorQueue() {
    IndependentReceiver device;Config cfg;cfg.frequency=5800000000ULL;cfg.rate=1000000;cfg.block_bytes=4096;cfg.analysis_queue_bytes=4096;
    ReceiverSession session(device.backend,cfg);Observation player;device.narrowReadback=true;
    auto invalid=session.acquire({5800000000.,220000,225001,100000000,{}},player.consumer());wait([&]{return player.state()=="readback_conflict";},"Invalid applied rate was accepted");wait([&]{return device.active==0;},"Rejected readback left hardware running");
    device.narrowReadback=false;Observation kept;auto playback=session.acquire({5800000000.,220000,225001,100000000,{}},kept.consumer());wait([&]{return kept.pairs>10000;},"Receiver did not recover after readback failure");
    // Exact 250 kS/s matches the independent recommendation already in use.
    cfg.rate=250000;cfg.auto_gain=false;cfg.analysis_queue_bytes=1024*1024;std::uint64_t delivered=0;
    const auto sharedReport=session.stream(cfg,[&](auto bytes,auto){delivered+=bytes.size()/4;},[&]{return delivered>=50000;},{});
    require(sharedReport.at("driver_report_final")==false && sharedReport.at("consumer_input_pairs")==delivered,"Shared monitoring invented final driver counters while playback continued");
    cfg.analysis_queue_bytes=4096;bool failed=false;
    try{session.stream(cfg,[](auto,auto){std::this_thread::sleep_for(std::chrono::milliseconds(25));},[]{return false;},{});}catch(const ReceptionError& e){failed=std::string(e.what()).find("monitor_input_queue_full")!=std::string::npos && e.status.diagnostic.at("stage")=="monitor_input" && e.status.diagnostic.at("rejected_pairs").get<unsigned>()>0 && e.status.diagnostic.at("queue_high_water_bytes")<=e.status.diagnostic.at("queue_capacity_bytes");}
    require(failed,"A slow monitor did not fail its bounded queue explicitly");const auto before=kept.pairs.load();wait([&]{return kept.pairs>before+50000;},"A full monitor queue interrupted independent playback");
    playback.reset();session.disconnect();
}
void driverReports() {
    IndependentReceiver device;Config cfg;cfg.rate=1000000;cfg.analysis_queue_bytes=1024*1024;ReceiverSession session(device.backend,cfg);std::uint64_t delivered=0;
    const auto report=session.stream(cfg,[&](auto bytes,auto){delivered+=bytes.size()/4;},[&]{return delivered>=20000;},{});
    require(report.at("driver_report_final")==true && report.at("read_stats").at("reads").get<unsigned>()>0,"The final driver report was lost when the last monitor lease stopped");
    device.failAfter=12000;bool preserved=false;
    try{session.stream(cfg,[](auto,auto){},[]{return false;},{});}catch(const SourceError& error){preserved=error.details.at("reason_code")=="stream_discontinuity" && error.details.at("driver_marker")=="independent-provider" && error.details.at("read_stats").at("reads")==5;}
    require(preserved,"Structured driver failure evidence was replaced by a generic consumer error");session.disconnect();
}
void compatibleUpgradeAndCombinedBand() {
    IndependentReceiver device;Config base;base.frequency=100000000;base.rate=10000000;ReceiverSession session(device.backend,base);
    Observation first,second,wide;
    auto radio=session.acquire({99000000,220000,225001,100000000,{}},first.consumer());wait([&]{return first.rate()==250000 && first.pairs>10000;},"Minimum-demand playback did not start economically");
    auto monitor=session.acquire({100000000,10000000,10000000,10000000,base},wide.consumer());wait([&]{return wide.pairs>10000 && first.rate()==10000000;},"A compatible wider monitor could not upgrade a playback-only input");
    auto another=session.acquire({101000000,220000,225001,100000000,{}},second.consumer());wait([&]{return second.pairs>10000;},"Second minimum-demand consumer did not share the input");
    monitor.reset();wait([&]{return first.rate()==8000000 && second.rate()==8000000;},"Economical replanning failed to preserve the union of both occupied bands");
    another.reset();wait([&]{return first.rate()==250000;},"Releasing a minimum-demand consumer did not reduce the remaining input");require(device.maximum==1,"Compatible upgrade opened a second physical device");radio.reset();session.disconnect();
}
void installedProvider() {
    // The separately built C ABI provider deliberately recommends 2 MS/s even
    // though its advertised preset is 4 MS/s, and supplies CF32 at 5.8 GHz.
    // Neither this session implementation nor the GUI learns that provider ID.
    const auto& backend=receiver_by_name("test.receiver");Config base;base.frequency=5800000000ULL;base.receiver_settings={{"profile","eco"},{"preamp",true}};
    ReceiverSession session(backend,base);Observation observer;auto lease=session.acquire({5800000000.,220000,225001,4000000,{}},observer.consumer());
    wait([&]{return observer.pairs>=50000;},"The independently installed binary provider did not stream through reception leases");
    {std::lock_guard lock(observer.mutex);const auto& conditions=observer.latest.conditions.json();require(conditions.at("frequency_calibration").at("evidence").at("reference_id")=="independent-fixture.clock" && conditions.at("adc_overload").at("state")=="not_applicable" && conditions.at("independent_future_condition").at("value")==42,"Independent binary startup evidence was lost or reinterpreted");}
    {std::lock_guard lock(observer.mutex);require(observer.latest.stream && observer.latest.stream->format==SampleFormat::cf32_le && observer.latest.stream->sample_rate==2000000 && observer.latest.requested.at("settings").at("profile")=="wide" && observer.latest.requested.at("settings").at("preamp")==false,"The session replaced plugin recommendations or assumed a fixed sample encoding / RF ceiling");}
    lease.reset();session.disconnect();
}
}
int main(){try{sharingAndLifetime();readbackAndMonitorQueue();driverReports();compatibleUpgradeAndCombinedBand();installedProvider();std::cout<<"Independent receiver: shared input, conflict preservation, sustained playback, compatible upgrade, combined-band replan, readback checks, bounded monitor isolation, truthful driver reports and a separate C ABI provider passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
