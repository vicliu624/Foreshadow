#include "signalscope/backend/sessions/replay_session.hpp"
#include "signalscope/adapters/modulation/fsk.hpp"
#include "signalscope/adapters/modulation/wideband_lora.hpp"
#include "signalscope/backend/sessions/wideband_narrowband.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace signalscope {
Json replaySession(const ReplayInfo& info,ReplaySessionOptions options,
                   const std::function<bool()>& paused,const std::function<bool()>& cancelled,
                   const ReplaySessionObserver& observer) {
    if(!std::isfinite(options.speed) || (options.speed!=0 && options.speed<.01) || options.speed>1000 ||
       !std::isfinite(options.lora.search_width) || options.lora.search_width<=0)
        throw std::invalid_argument("Invalid replay speed or search width");
    options.lora.rate=info.rate;
    options.lora.receiver_frequency=info.frequency;
    const auto stride=bytes_per_pair(info.format);
    IqPublisher analysisIq(observer.analysis_iq,{info.rate,info.frequency,info.format});
    auto lora=options.enableLora?std::make_unique<WidebandLoRa>(options.lora,info.format):nullptr;
    auto spectrum=observer.spectrum?std::make_unique<Spectrometer>(info.format):nullptr;
    Json recent=Json::array();
    NarrowbandEvents narrowbandEvents;
    auto& selection=options.narrowband;
    const auto searchWidth=options.lora.search_width;
    selection.broadcast_fm &= overlaps_broadcast_fm(info.frequency,searchWidth);
    const bool supported=info.rate>=225001 && info.rate<=100000000 &&
                         info.frequency<=std::numeric_limits<std::uint32_t>::max();
    Config config;
    config.rate=info.rate;
    if(supported)config.frequency=static_cast<std::uint32_t>(std::llround(info.frequency));
    auto narrow=supported && selection.any()?std::make_unique<WidebandNarrowband>(config,searchWidth,info.format,selection):nullptr;
    std::uint64_t processed=0;
    using Clock=std::chrono::steady_clock;
    auto deadline=Clock::now();
    auto lastReport=deadline-std::chrono::seconds(1);
    const auto blockPairs=narrow && selection.broadcast_fm?std::max(32768u,info.rate/50):32768u;
    const auto statusFor=[&](const char* state){
        Json status{{"state",state},{"source","recorded"},{"source_pairs",info.pairs},
                    {"processed_pairs",processed},{"analysis_dropped_pairs",0},{"recent_lora_events",recent}};
        narrowbandEvents.publish(status);
        if(lora)status["wideband_analysis"]=lora->status();
        if(narrow)status["narrowband_analysis"]=narrow->status();
        return status;
    };
    replay(info,0,info.pairs,0,blockPairs*stride,[&](auto bytes,auto first){
        bool waited=false;
        while(paused() && !cancelled()) {
            waited=true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if(waited)deadline=Clock::now();
        if(cancelled())return;
        if(lora)lora->feed(bytes,first);
        processed=first+bytes.size()/stride;
        analysisIq.publish(bytes,first);
        if(narrow)narrow->feed(bytes,first,narrowbandEvents);
        if(lora)for(auto& event:lora->take_events()) {
            if(event.value("evidence_level",std::string())!="lora_structure_match")continue;
            recent.push_back(std::move(event));
            if(recent.size()>32)recent.erase(recent.begin());
        }
        if(Clock::now()-lastReport>=std::chrono::milliseconds(250)) {
            if(spectrum) {
                auto frame=spectrum->measure(bytes,first,info.rate,info.frequency);
                frame.search_width_hz=searchWidth;
                observer.spectrum(frame);
            }
            if(observer.status)observer.status(statusFor("replaying"));
            lastReport=Clock::now();
        }
        if(options.speed>0) {
            const auto seconds=double(bytes.size()/stride)/info.rate/options.speed;
            deadline+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
            while(Clock::now()<deadline && !cancelled() && !paused())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    },cancelled);
    if(lora) {
        lora->finish();for(auto& event:lora->take_events()) {
            if(event.value("evidence_level",std::string())!="lora_structure_match")continue;
            recent.push_back(std::move(event));if(recent.size()>32)recent.erase(recent.begin());
        }
    }
    if(narrow)narrow->finish(narrowbandEvents);
    auto status=statusFor(cancelled()?"stopped":"replay_finished");
    if(observer.status)observer.status(status);
    return status;
}
}
