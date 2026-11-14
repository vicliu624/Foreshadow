#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"
#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/adapters/receivers/receiver.hpp"
#include "signalscope/backend/sessions/snapshot_analysis.hpp"
#include <cmath>
#include <numbers>
#include <iostream>
#include <future>
#include <thread>
#include <limits>
using namespace signalscope;
void require(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } require(threw,"expected rejection"); }

void checkIqContinuity() {
    std::vector<bool> gaps;
    IqSubscribers subscribers;
    subscribers.add([&](const IqBlockView& block){gaps.push_back(block.discontinuous);});
    const IqStreamParameters stream{1'000'000,470'500'000,SampleFormat::ci16_le};
    IqPublisher capture(subscribers,stream);
    IqPublisher analysis(subscribers,stream);
    const Bytes block(8,0);
    capture.publish(block,0);
    analysis.publish(block,0);
    capture.publish(block,2);
    capture.publish(block,4);
    analysis.publish(block,4);
    analysis.publish(block,6);
    require(gaps==std::vector<bool>{false,false,false,false,true,false},
            "I/Q subscription gaps must follow each delivery path independently");
}

void checkSnapshotCollector(SampleFormat format) {
    const auto stride=bytes_per_pair(format);
    const auto block=[&](std::size_t pairs,bool strong){
        const auto sample=format==SampleFormat::cu8
            ?(strong?Bytes{210,220}:Bytes{128,128})
            :(strong?Bytes{0,64,0,64}:Bytes{1,0,1,0});
        Bytes bytes;
        for(std::size_t i=0;i<pairs;++i)bytes.insert(bytes.end(),sample.begin(),sample.end());
        return bytes;
    };
    const auto quiet=block(16,false);
    const auto loud=block(96,true);
    SnapshotCollector collector(256,470500000,1000000,format);
    require(collector.feed(quiet,0).bytes.empty(),"Quiet samples unexpectedly triggered a snapshot");
    const auto snapshot=collector.feed(loud,16);
    require(snapshot.first==0 && snapshot.bytes.size()==64*stride && snapshot.format==format &&
            std::equal(quiet.begin(),quiet.end(),snapshot.bytes.begin()),
            "Snapshot lost pre-trigger samples or exceeded its bound");
    require(collector.feed(loud,112).bytes.empty(),"Snapshot ignored the cooldown");

    SnapshotCollector interrupted(256,470500000,1000000,format);
    interrupted.feed(quiet,0);
    require(interrupted.feed(block(16,true),16).bytes.empty(),"Short burst published an incomplete snapshot");
    const auto afterGap=interrupted.feed(loud,1000);
    require(afterGap.first==1000 && afterGap.bytes.size()==64*stride &&
            std::equal(afterGap.bytes.begin(),afterGap.bytes.end(),loud.begin()),
            "Snapshot concatenated samples across a discontinuity");
}

int main() {
    try {
        const auto backends=receiver_backends();
        require(backends.size()>=2,"receiver registry is missing built-in backends");
        require(&default_receiver()==find_receiver("rtlsdr"),"Default receiver changed for existing installations");
        for(const auto* receiver:backends) {
            require(receiver->id && receiver->display_name && find_receiver(receiver->id)==receiver,
                    "receiver registry contains a duplicate or incomplete identity");
        }
        require(find_receiver("unknown")==nullptr,"unknown receiver unexpectedly resolved");
        const auto rtlGains=receiver_by_name("rtlsdr").gain_presets_tenths;
        require(!rtlGains.empty() && rtlGains.front()==197 &&
                receiver_by_name("sdrplay").gain_presets_tenths.empty(),"backend gain presets changed");
        checkSnapshotCollector(SampleFormat::cu8);
        checkSnapshotCollector(SampleFormat::ci16_le);
        checkIqContinuity();
        LoRaConfig timing;
        timing.bandwidth_candidates={125000,250000,500000};
        timing.rate=2'048'000;
        require(!loraTimingSupported(timing),"2.048 MS/s must reject incompatible LoRa symbol timing");
        timing.rate=2'000'000;
        require(loraTimingSupported(timing),"2 MS/s must accept the default LoRa bandwidths");
        timing.rate=3'000'000;
        require(loraTimingSupported(timing),"3 MS/s must accept the default LoRa bandwidths");
        BoundedQueue<int> queue(2);
        require(queue.try_push(1) && queue.try_push(2),"enqueue");
        require(!queue.try_push(3),"overflow must be visible");
        queue.close(); int value=0;
        require(queue.pop(value) && value==1,"FIFO first");
        require(queue.pop(value) && value==2,"FIFO second");
        require(!queue.pop(value) && !queue.try_push(4),"closed queue");
        require(queue.high_water()==2,"high water");
        BoundedQueue<int> waiting(1);
        auto consumer=std::async(std::launch::async,[&]{int v;return waiting.pop(v);});
        waiting.close(); require(!consumer.get(),"close must wake blocked consumer");
        Config config; config.validate();
        config.queue_blocks=0; rejects([&]{config.validate();});
        require(sample_count(.001,2000000)==2000,"exact sample duration");
        require(sample_count(90000,2000000)==180000000000ULL,"more than 24 hours must not overflow");
        Config continuous;continuous.seconds=0;continuous.validate();
        Config buffering;buffering.analysis_queue_bytes=0;rejects([&]{buffering.validate();});
        buffering=Config{};buffering.analysis_queue_seconds=std::numeric_limits<double>::infinity();rejects([&]{buffering.validate();});
        rejects([]{ sample_count(std::numeric_limits<double>::quiet_NaN(),2000000); });
        PowerStats power; const Bytes full={0,255,255,0}; power.add(full);
        require(power.pairs==2 && power.clipped_pairs==2,"clipping");
        rejects([&]{power.add(Bytes{0});});
        Spectrometer spectrum;Bytes tone(2048);
        for(std::size_t n=0;n<1024;++n) {
            const auto phase=2*std::numbers::pi*128*double(n)/1024;
            tone[n*2]=static_cast<std::uint8_t>(std::lround(127.5+32*std::cos(phase)));
            tone[n*2+1]=static_cast<std::uint8_t>(std::lround(127.5+32*std::sin(phase)));
        }
        const auto frame=spectrum.measure(tone,100,2000000,470500000);
        const auto peak=std::max_element(frame.bins_dbfs.begin(),frame.bins_dbfs.end())-frame.bins_dbfs.begin();
        require(peak==640,"spectrum frequency axis must preserve positive frequency");
        require(std::abs(frame.power_dbfs+12.0412)<.15,"digital power normalization");
        LoRaConfig lc;lc.sf_min=lc.sf_max=7;lc.workers=1;
        LoRaDetector detector(lc);detector.feed(tone,0);
        rejects([&]{detector.feed(tone,9999);});
        detector.reset();detector.feed(tone,0);
        require(detector.results()["source_pairs"]==1024,"gap reset must reuse plans and restart sample coordinates");
        std::cout<<"核心契约检查通过\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
