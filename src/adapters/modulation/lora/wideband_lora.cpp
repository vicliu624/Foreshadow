#include "signalscope/adapters/modulation/wideband_lora.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/backend/dsp/parallel_channels.hpp"
#include <cmath>

namespace signalscope {
LoRaAnalysisPlan planWidebandLoRa(const LoRaConfig& c) {
    const auto widths=c.bandwidth_candidates.empty()?std::vector<double>{c.bandwidth}:c.bandwidth_candidates;
    if(!c.rate || c.rate>100000000 || c.workers<1 || c.workers>32 || !std::isfinite(c.search_width) || c.search_width<=0 || c.search_width>c.rate || widths.size()>4 ||
       !std::isfinite(c.peak_to_mean_db) || c.peak_to_mean_db<6 || c.peak_to_mean_db>60 || c.repeat_symbols<3 || c.repeat_symbols>32 ||
       (!c.normal_iq && !c.inverted_iq) || c.probe_stride_symbols<1 || c.probe_stride_symbols>4 ||
       (c.time_phases!=1 && c.time_phases!=2 && c.time_phases!=4 && c.time_phases!=8))
        throw std::invalid_argument("Invalid LoRa source or search configuration");
    for(std::size_t i=0;i<widths.size();++i)
        if(!std::isfinite(widths[i]) || widths[i]<=0 || widths[i]>=c.rate || widths[i]>25000000 ||
           std::find(widths.begin(),widths.begin()+i,widths[i])!=widths.begin()+i)
            throw std::invalid_argument("Invalid or duplicated LoRa bandwidth");
    const auto maximum=*std::max_element(widths.begin(),widths.end());
    if(c.rate<=3200000 && c.search_width+maximum<=c.rate && loraTimingSupported(c))return {true,c.rate,{{0,c.search_width}}};
    const auto rate=static_cast<std::uint32_t>(std::llround(4*maximum));auto local=c;local.rate=rate;
    if(!loraTimingSupported(local))throw std::invalid_argument("Selected LoRa bandwidths cannot share an analysis clock");
    const auto minimum=*std::min_element(widths.begin(),widths.end());
    // Spacing accommodates the largest BW, while the outer channels also
    // reach every complete signal of the smallest selected BW.
    auto channels=planAnalysisChannels(c.rate,rate,c.search_width,maximum,minimum);
    return {false,rate,std::move(channels)};
}
class WidebandLoRa::Impl {
public:
    Impl(const LoRaConfig& requested,SampleFormat f):config(requested),format(f) {
        const auto widths=config.bandwidth_candidates.empty()?std::vector<double>{config.bandwidth}:config.bandwidth_candidates;
        const auto maximum=*std::max_element(widths.begin(),widths.end());
        const auto plan=planWidebandLoRa(config);config.bandwidth=maximum;
        if(plan.direct) {
            direct=std::make_unique<LoRaDetector>(config,format);return;
        }
        analysis_rate=plan.analysis_rate;
        auto local=config;local.rate=analysis_rate;local.workers=1;local.bandwidth_candidates=widths;
        local.search_width=std::min(.5*analysis_rate,double(config.rate)-maximum);
        channelizer=std::make_unique<Channelizer>(config.rate,analysis_rate,format,plan.channels);
        for(const auto& channel:channelizer->channels()) {
            local.receiver_frequency=config.receiver_frequency+channel.offset_hz;local.search_width=channel.search_width_hz;
            detectors.push_back(std::make_unique<LoRaDetector>(local,SampleFormat::ci16_le,true));
        }
        pending.resize(detectors.size());first_output.resize(detectors.size());buckets.resize(detectors.size());
        const auto search_count=detectors.size()*widths.size()*(config.sf_max-config.sf_min+1)*
            (unsigned(config.normal_iq)+unsigned(config.inverted_iq));
        const auto count=std::min<std::size_t>(config.workers,search_count);
        workers=std::make_unique<ParallelChannels>(count);
    }
    void collect(const ChannelizedBlock& block) {
        auto& values=pending[block.channel];if(values.empty())first_output[block.channel]=block.first_sample;
        values.insert(values.end(),block.samples.begin(),block.samples.end());
    }
    void dispatch() {
        std::vector<LoRaFloatBlock> inputs;
        for(std::size_t channel=0;channel<detectors.size();++channel)if(!pending[channel].empty())
            inputs.push_back({detectors[channel].get(),pending[channel],first_output[channel]});
        feedLoRaChannels(inputs,*workers);
        for(std::size_t channel=0;channel<detectors.size();++channel)buckets[channel]=detectors[channel]->take_events();
        const auto channels=channelizer->channels();
        for(std::size_t channel=0;channel<buckets.size();++channel)for(auto& event:buckets[channel]) {
            const auto translate=[&](std::uint64_t value) {
                const auto source=static_cast<long double>(value)*config.rate/analysis_rate-channelizer->delay_input_samples();
                return source<=0?std::uint64_t{0}:static_cast<std::uint64_t>(std::llround(source));
            };
            for(const auto* key:{"first_observed_sample","confirmed_at_sample","sfd_start_sample","sfd_end_sample"})
                if(event.contains(key))event[key]=translate(event[key].get<std::uint64_t>());
            if(event.value("confirmed_at_sample",std::uint64_t{})>pairs)continue;
            const auto center=event.value("center_frequency_hz",config.receiver_frequency);
            if(event.contains("center_frequency_hz") && std::abs(center-config.receiver_frequency)>config.search_width/2)continue;
            event["analysis_channel"]={{"offset_hz",channels[channel].offset_hz},{"sample_rate_hz",analysis_rate},
                {"source_sample_rate_hz",config.rate},{"source_index_mapping",true}};
            if(event.contains("dechirp_peak_offset_hz"))event["dechirp_peak_offset_hz"]=event["dechirp_peak_offset_hz"].get<double>()+channels[channel].offset_hz;
            if(event.contains("fullband_power_dbfs")){event["analysis_channel_power_dbfs"]=event["fullband_power_dbfs"];event.erase("fullband_power_dbfs");}
            bool duplicate=false;
            if(event.value("evidence_level",std::string())=="lora_structure_match") {
                const auto bandwidth=event.at("bandwidth_hz").get<double>();const auto sf=event.at("sf_candidate").get<int>();
                const auto sfd=event.at("sfd_start_sample").get<std::uint64_t>();
                for(const auto& previous:recent)
                    if(previous.at("bandwidth_hz")==event.at("bandwidth_hz") && previous.at("sf_candidate")==sf &&
                       previous.at("iq_inverted")==event.at("iq_inverted") &&
                       std::abs(previous.at("center_frequency_hz").get<double>()-center)<bandwidth/8 &&
                       std::abs(double(previous.at("sfd_start_sample").get<std::uint64_t>())-sfd)<config.rate*std::ldexp(1.,sf)/bandwidth/4)
                        {duplicate=true;break;}
                if(!duplicate){recent.push_back(event);if(recent.size()>128)recent.erase(recent.begin());}
            }
            if(!duplicate){if(events.size()<2048)events.push_back(std::move(event));else ++omitted;}
        }
        for(auto& bytes:pending)bytes.clear();
    }
    LoRaConfig config;SampleFormat format;
    std::unique_ptr<LoRaDetector> direct;
    std::unique_ptr<Channelizer> channelizer;
    std::uint32_t analysis_rate=0;
    std::vector<std::unique_ptr<LoRaDetector>> detectors;
    std::vector<std::vector<std::complex<float>>> pending;std::vector<std::uint64_t> first_output;std::vector<Json> buckets;
    std::unique_ptr<ParallelChannels> workers;
    bool finished=false;
    std::uint64_t pairs=0,omitted=0;Json events=Json::array(),recent=Json::array();
};
WidebandLoRa::WidebandLoRa(const LoRaConfig& config,SampleFormat format):impl_(std::make_unique<Impl>(config,format)){}
WidebandLoRa::~WidebandLoRa()=default;
void WidebandLoRa::feed(std::span<const std::uint8_t> bytes,std::uint64_t first) {
    auto& s=*impl_;if(first!=s.pairs || s.finished || bytes.size()%bytes_per_pair(s.format))throw std::invalid_argument("LoRa source is discontinuous");
    s.pairs+=bytes.size()/bytes_per_pair(s.format);
    if(s.direct){s.direct->feed(bytes,first);return;}
    s.channelizer->feed(bytes,[&](const auto& block){s.collect(block);});s.dispatch();
}
void WidebandLoRa::finish() {
    auto& s=*impl_;if(s.finished)return;s.finished=true;if(s.direct)return;
    s.channelizer->finish([&](const auto& block){s.collect(block);});s.dispatch();
}
void WidebandLoRa::reset() {
    auto& s=*impl_;s.pairs=0;s.omitted=0;s.finished=false;s.events=Json::array();s.recent=Json::array();
    if(s.direct){s.direct->reset();return;}
    s.channelizer->reset();for(auto& d:s.detectors)d->reset();for(auto& b:s.pending)b.clear();
}
Json WidebandLoRa::take_events(){if(impl_->direct)return impl_->direct->take_events();auto result=std::move(impl_->events);impl_->events=Json::array();return result;}
Json WidebandLoRa::status() const {
    const auto& s=*impl_;Json channels=Json::array();
    if(s.channelizer)for(const auto& channel:s.channelizer->channels())channels.push_back({{"offset_hz",channel.offset_hz},{"search_width_hz",channel.search_width_hz}});
    return {{"mode",s.direct?"source_rate":"overlapping_channels"},{"source_rate_hz",s.config.rate},
        {"analysis_rate_hz",s.direct?s.config.rate:s.analysis_rate},{"channel_count",s.direct?1:s.detectors.size()},
        {"workers",s.direct?s.config.workers:s.workers->workers()},{"channels",channels},{"source_pairs",s.pairs},
        {"requested_span_hz",s.config.search_width},{"omitted_events",s.omitted}};
}
Json WidebandLoRa::results() const {
    if(impl_->direct)return impl_->direct->results();
    auto work=Json::array();
    for(std::size_t i=0;i<impl_->detectors.size();++i) {
        const auto report=impl_->detectors[i]->results();
        for(auto entry:report.at("search_work")) {
            entry["analysis_channel_offset_hz"]=impl_->channelizer->channels()[i].offset_hz;work.push_back(std::move(entry));
        }
    }
    return {{"algorithm","channelized_lora_v1"},{"source_pairs",impl_->pairs},{"events",impl_->events},
        {"analysis",status()},{"search_work",std::move(work)},{"scores_calibrated",false},{"omitted_events",impl_->omitted}};
}
}
