#include "signalscope/backend/sessions/wideband_narrowband.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/backend/dsp/parallel_channels.hpp"
#include <cmath>

namespace signalscope {
struct WidebandNarrowband::Impl {
    Config source,local;
    double width;
    NarrowbandSelection selection;
    SampleFormat format;
    std::unique_ptr<Channelizer> channelizer;
    std::vector<std::unique_ptr<FskDetector>> detectors;
    std::vector<Bytes> pending;
    std::vector<std::uint64_t> starts;
    std::unique_ptr<ParallelChannels> executor;
    std::unique_ptr<FskDetector> direct;
    Bytes directPending;std::uint64_t directFirst=0;
    std::uint64_t next=0,origin=0,gaps=0;
    bool started=false;
    static constexpr std::uint32_t analysisRate=2000000;
    Impl(const Config& cfg,double span,SampleFormat format,NarrowbandSelection modes)
        :source(cfg),local(cfg),width(span),selection(modes),format(format) {
        if(!cfg.rate || cfg.rate>100000000 || !std::isfinite(span) || span<=0 || span>cfg.rate)
            throw std::invalid_argument("Invalid narrowband source span or clock");
        local.rate=analysisRate;
        if(cfg.rate<=3200000){direct=std::make_unique<FskDetector>(format,selection);return;}
        channelizer=std::make_unique<Channelizer>(cfg.rate,analysisRate,format,planAnalysisChannels(cfg.rate,analysisRate,span,
            modes.broadcast_fm?220000:40000));
        for(std::size_t i=0;i<channelizer->channels().size();++i)
            detectors.push_back(std::make_unique<FskDetector>(SampleFormat::ci16_le,selection,false));
        pending.resize(detectors.size());starts.resize(detectors.size());
        executor=std::make_unique<ParallelChannels>(std::min<std::size_t>(detectors.size(),recommendedAnalysisWorkers()));
    }
    void reset(std::uint64_t first) {
        origin=first;if(channelizer)channelizer->reset();directPending.clear();directFirst=first;
        if(direct)direct=std::make_unique<FskDetector>(format,selection);
        for(auto& d:detectors)d=std::make_unique<FskDetector>(SampleFormat::ci16_le,selection,false);
        for(auto& b:pending)b.clear();
    }
    void emit(Json event,bool fm,NarrowbandEvents& results) {
        if(event.is_null())return;
        if(std::abs(event.at("center_frequency_hz").get<double>()-source.frequency)>width/2)return;
        for(const auto* key:{"first_observed_sample","confirmed_at_sample"}) {
            const auto sample=static_cast<long double>(event.at(key).get<std::uint64_t>())*source.rate/analysisRate-channelizer->delay_input_samples();
            event[key]=origin+(sample<=0?0:static_cast<std::uint64_t>(std::llround(sample)));
        }
        if(event.at("confirmed_at_sample").get<std::uint64_t>()>next)return;
        event["analysis_sample_rate_hz"]=analysisRate;
        results.append(fm?Json{}:event,fm?event:Json{},source.rate);
    }
    void analyze(std::size_t index,NarrowbandEvents& results,std::size_t count=0) {
        auto& bytes=pending[index];if(bytes.empty())return;
        if(!count)count=bytes.size();
        const auto& channel=channelizer->channels()[index];
        if(source.frequency+channel.offset_hz<0 || source.frequency+channel.offset_hz>4294967295.){bytes.clear();return;}
        auto config=local;config.frequency=static_cast<std::uint32_t>(std::llround(source.frequency+channel.offset_hz));
        auto detected=detectors[index]->detectAll(std::span(bytes).first(count),starts[index],config,channel.search_width_hz);
        emit(std::move(detected.candidate),false,results);
        for(auto& fm:detected.broadcasts)emit(std::move(fm),true,results);
        starts[index]+=count/4;bytes.erase(bytes.begin(),bytes.begin()+count);
    }
    void collect(const ChannelizedBlock& block) {
        auto& bytes=pending[block.channel];
        if(bytes.empty())starts[block.channel]=block.first_sample;
        for(const auto z:block.samples)for(const auto part:{z.real(),z.imag()}) {
            const auto v=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(std::clamp(part,-1.f,32767.f/32768)*32768)));
            bytes.push_back(v&255);bytes.push_back(v>>8);
        }
    }
    void dispatch(NarrowbandEvents& results,bool finish=false) {
        constexpr std::size_t batch=4*40960;
        if(!finish && std::none_of(pending.begin(),pending.end(),[](const auto& bytes){return bytes.size()>=batch;}))return;
        std::vector<Json> buckets(detectors.size());
        executor->run(detectors.size(),[&](std::size_t index){
            NarrowbandEvents events;
            // FM pilot confirmation needs contiguous data, independent of USB read size.
            while(pending[index].size()>=batch)analyze(index,events,batch);
            if(finish)analyze(index,events);
            events.publish(buckets[index]);
        });
        for(const auto& bucket:buckets) {
            for(const auto& event:bucket.at("recent_fsk_events"))results.append(event,nullptr,source.rate);
            for(const auto& event:bucket.at("recent_ask_events"))results.append(event,nullptr,source.rate);
            for(const auto& event:bucket.at("recent_fm_events"))results.append(nullptr,event,source.rate);
        }
    }
    void feedDirect(std::span<const std::uint8_t> bytes,NarrowbandEvents& results,bool finish=false) {
        directPending.insert(directPending.end(),bytes.begin(),bytes.end());
        // FFT history uses a fixed 256-sample hop; preserve that grid across
        // acquisition blocks, including fractional-rate clocks and USB short reads.
        const auto stride=bytes_per_pair(format),target=std::clamp<std::size_t>(source.rate/50/256*256,512,65536)*stride;
        std::size_t consumed=0;
        while(directPending.size()-consumed>=target || (finish && consumed<directPending.size())) {
            const auto count=std::min(target,directPending.size()-consumed);
            auto detected=direct->detectAll(std::span(directPending).subspan(consumed,count),directFirst,source,width);
            results.append(std::move(detected.candidate),nullptr,source.rate);
            for(auto& fm:detected.broadcasts)results.append(nullptr,std::move(fm),source.rate);
            consumed+=count;directFirst+=count/stride;
        }
        directPending.erase(directPending.begin(),directPending.begin()+consumed);
    }
};
WidebandNarrowband::WidebandNarrowband(const Config& c,double w,SampleFormat f,NarrowbandSelection s)
    :impl_(std::make_unique<Impl>(c,w,f,s)){}
WidebandNarrowband::~WidebandNarrowband()=default;
void WidebandNarrowband::feed(std::span<const std::uint8_t> bytes,std::uint64_t first,NarrowbandEvents& results) {
    auto& s=*impl_;
    if(bytes.size()%bytes_per_pair(s.format))throw std::invalid_argument("Incomplete narrowband I/Q pair");
    if(!s.started || first!=s.next){if(s.started)++s.gaps;s.reset(first);s.started=true;}
    s.next=first+bytes.size()/bytes_per_pair(s.format);
    if(s.direct){s.feedDirect(bytes,results);return;}
    s.channelizer->feed(bytes,[&](const auto& block){s.collect(block);});s.dispatch(results);
}
void WidebandNarrowband::finish(NarrowbandEvents& results) {
    auto& s=*impl_;if(s.direct){s.feedDirect({},results,true);return;}s.channelizer->finish([&](const auto& block){s.collect(block);});s.dispatch(results,true);
}
Json WidebandNarrowband::status() const {
    const auto& s=*impl_;return {{"analysis_rate_hz",s.direct?s.source.rate:Impl::analysisRate},{"channel_count",s.direct?1:s.detectors.size()},
        {"requested_span_hz",s.width},{"source_gaps",s.gaps},{"source_position",s.next},{"workers",s.direct?1:s.executor->workers()}};
}
}
