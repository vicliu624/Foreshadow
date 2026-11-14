#include "signalscope/backend/sessions/receiver_configuration.hpp"
#include <algorithm>
#include <cmath>
#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/adapters/modulation/wideband_lora.hpp"

namespace signalscope {
ReceiverPlan planMonitorConfiguration(const ReceiverCapabilities& caps,const Config& cfg,const LoRaConfig& lc,
                                      bool lora,bool narrowband,bool snapshots) {
    auto p=planReceiverConfiguration(caps,cfg);
    const auto issue=[&](const char* code,const char* field,Json values=Json::object()) {
        p.issues.push_back({code,field,"analysis",true,std::move(values)});
    };
    double margin=0;
    if(lora) {
        margin=lc.bandwidth_candidates.empty()?lc.bandwidth:
            *std::max_element(lc.bandwidth_candidates.begin(),lc.bandwidth_candidates.end());
        if(lc.search_width>0 && lc.search_width<=cfg.rate)try {
            auto requested=lc;requested.rate=cfg.rate;requested.receiver_frequency=cfg.frequency;
            const auto analysis=planWidebandLoRa(requested);
            p.lora_analysis_rate_hz=analysis.analysis_rate;
            p.lora_channels=analysis.channels.size();
        } catch(const std::invalid_argument&) {issue("lora_bandwidth","sample_rate_hz",{{"bandwidth_hz",margin},{"rate_hz",cfg.rate}});}
    }
    p.complete_signal_search_width_hz=std::max(0.,double(cfg.rate)-margin);
    if(!std::isfinite(lc.search_width) || lc.search_width<=0 ||
       ((lora || narrowband || snapshots) && lc.search_width>p.maximum_search_width_hz))
        issue("search_passband","search_width_hz",{{"maximum_hz",p.maximum_search_width_hz},{"requested_hz",lc.search_width}});
    if(margin>0 && lc.search_width>p.complete_signal_search_width_hz)
        p.issues.push_back({"detector_edge_coverage","search_width_hz","information",false,{{"complete_centers_hz",p.complete_signal_search_width_hz},{"bandwidth_hz",margin}}});
    if(p.filter_bandwidth_hz>0 && lc.search_width>p.filter_bandwidth_hz)
        p.issues.push_back({"filter_rolloff","if_bandwidth_hz","information",false,{{"filter_hz",p.filter_bandwidth_hz}}});
    if(p.valid())p.issues.push_back({"throughput_unverified","sample_rate_hz","information",false});
    return p;
}
std::optional<MonitorRecommendation> recommendMonitorConfiguration(
    const ReceiverCapabilities& caps,const Config& requested,const LoRaConfig& detectors,
    bool lora,bool narrowband,bool snapshots) {
    // The plugin owns hardware choices; the host only checks detector needs.
    for(const auto& cfg:receiverRecommendations(caps,requested)) {
        if(cfg.frequency!=requested.frequency)continue;
        auto lc=detectors;
        const auto limit=planMonitorConfiguration(caps,cfg,lc,lora,narrowband,snapshots).maximum_search_width_hz;
        lc.search_width=std::floor(limit/1000)*1000;
        if(lc.search_width>=1000 && planMonitorConfiguration(caps,cfg,lc,lora,narrowband,snapshots).valid())
            return MonitorRecommendation{cfg,lc.search_width};
    }
    return std::nullopt;
}
void requireMonitorConfiguration(const ReceiverPlan& p) {
    requireReceiverConfiguration(p);
    for(const auto& i:p.issues)if(i.blocking)
        throw std::invalid_argument("Invalid analysis configuration: "+i.code+" ("+i.field+")");
}
}
