#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include "../common/snapshot_fft.hpp"
#include "snapshot_registry.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace signalscope {
namespace {
using namespace snapshot;

std::vector<double> discover(std::span<const Complex> samples,double rate,double width) {
    constexpr std::size_t size=2048;
    Fft fft(size);
    std::array<double,size> power{};
    std::array<float,size> window{};
    for(std::size_t i=0;i<size;++i)window[i]=static_cast<float>(.5-.5*std::cos(tau*i/(size-1)));
    const auto blocks=samples.size()/size;
    if(!blocks)return {};
    for(std::size_t block=0;block<blocks;++block) {
        for(std::size_t i=0;i<size;++i)fft.set(i,samples[block*size+i]*window[i]);
        fft.run();
        for(std::size_t i=0;i<size;++i)power[i]+=fft.power(i);
    }
    auto sorted=power;
    auto median=sorted.begin()+size/2;
    std::nth_element(sorted.begin(),median,sorted.end());
    std::vector<double> result;
    for(std::size_t n=0;n<4;++n) {
        std::size_t peak=size;
        double largest=std::max(1e-12,*median*8);
        for(std::size_t k=0;k<size;++k) {
            const auto hz=binFrequency(k,size,rate);
            if(std::abs(hz)<=width/2 && std::abs(hz)+channelWidth/2<rate/2 && power[k]>largest) {
                peak=k;largest=power[k];
            }
        }
        if(peak==size)break;
        const auto offset=binFrequency(peak,size,rate);
        result.push_back(offset);
        for(std::size_t k=0;k<size;++k)
            if(std::abs(binFrequency(k,size,rate)-offset)<channelWidth)power[k]=0;
    }
    return result;
}

std::vector<Complex> extract(std::span<const Complex> samples,double rate,double offset,std::size_t decimation) {
    const auto radius=4*decimation;
    std::vector<Complex> taps(2*radius+1);
    const double cutoff=19000/rate;
    double gain=0;
    for(std::size_t k=0;k<taps.size();++k) {
        const double x=double(k)-radius;
        const double a=tau*cutoff*x;
        const double h=2*cutoff*(x==0?1.:std::sin(a)/a)*(.5+.5*std::cos(std::numbers::pi*x/radius));
        taps[k]=std::polar(1.f,static_cast<float>(-tau*offset*x/rate))*static_cast<float>(h);
        gain+=h;
    }
    std::vector<Complex> channel;
    channel.reserve(samples.size()/decimation);
    for(std::size_t i=radius;i+radius<samples.size();i+=decimation) {
        Complex value{};
        for(std::size_t k=0;k<taps.size();++k)value+=samples[i-radius+k]*taps[k];
        channel.push_back(value*std::polar(1.f,static_cast<float>(std::remainder(-tau*offset*i/rate,tau)))/
                          static_cast<float>(gain));
    }
    return channel;
}

}

Json detectSnapshot(std::span<const std::uint8_t> bytes,SampleFormat format,std::uint32_t rate,
               double center,double width,std::uint64_t first,SnapshotSelection selection) {
    Json results=Json::array();
    const auto stride=bytes_per_pair(format);
    if(!selection.any() || rate<225001 || rate>100000000 || bytes.size()%stride || bytes.size()/stride>rate/3 ||
       !std::isfinite(center) || !std::isfinite(width) || width<=0 || width>rate)return results;
    std::vector<Complex> samples;
    samples.reserve(bytes.size()/stride);
    for(std::size_t i=0;i<bytes.size();i+=stride) {
        const auto* p=bytes.data()+i;
        samples.push_back(read_iq_pair(p,format));
    }
    const WidebandInput input{samples,rate,center,width,first};
    for(const auto& detector:widebandDetectors())
        if(detector.enabled(selection))detector.analyze(input,results);
    const auto classifiers=burstDetectors();
    if(!std::ranges::any_of(classifiers,[&](const auto& detector){return detector.enabled(selection);}))
        return results;
    const auto offsets=discover(samples,rate,width);
    if(offsets.empty())return results;
    const auto decimation=std::max<std::size_t>(1,rate/80000);
    const auto channelRate=rate>3200000?80000.:double(rate)/decimation;
    std::vector<std::vector<Complex>> extracted(offsets.size());
    std::vector<double> actualOffsets=offsets;
    double delay=0;
    if(rate>3200000) {
        std::vector<AnalysisChannel> channels;for(auto offset:offsets)channels.push_back({offset,channelWidth});
        Channelizer converter(rate,80000,format,std::move(channels));delay=converter.delay_input_samples();
        const auto collect=[&](const ChannelizedBlock& b){auto& out=extracted[b.channel];out.insert(out.end(),b.samples.begin(),b.samples.end());};
        converter.feed(bytes,collect);converter.finish(collect);
        for(std::size_t i=0;i<offsets.size();++i)actualOffsets[i]=converter.channels()[i].offset_hz;
    } else for(std::size_t i=0;i<offsets.size();++i)extracted[i]=extract(samples,rate,offsets[i],decimation);
    for(std::size_t channelIndex=0;channelIndex<offsets.size();++channelIndex) {
        const auto offset=actualOffsets[channelIndex];const auto& channel=extracted[channelIndex];
        const auto hop=std::max<std::size_t>(1,static_cast<std::size_t>(channelRate/1000));
        std::vector<double> powers;
        for(std::size_t i=0;i+hop<=channel.size();i+=hop) {
            double sum=0;
            for(std::size_t k=i;k<i+hop;++k)sum+=std::norm(channel[k]);
            powers.push_back(sum/hop);
        }
        if(powers.empty())continue;
        auto ordered=powers;
        auto floor=ordered.begin()+ordered.size()/5;
        std::nth_element(ordered.begin(),floor,ordered.end());
        const auto threshold=std::max(*floor*8,1e-10);
        for(std::size_t i=0;i<powers.size();) {
            if(powers[i]<threshold){++i;continue;}
            auto end=i+1;
            while(end<powers.size() && powers[end]>=threshold)++end;
            // Trim one power window at either edge before carrier estimation.
            if(end>i+3) {
                const auto burst=std::span(channel).subspan((i+1)*hop,(end-i-2)*hop);
                Json event;
                for(const auto& detector:classifiers) {
                    if(!detector.enabled(selection))continue;
                    event=detector.identify(burst,channelRate,selection);
                    if(!event.is_null())break;
                }
                if(!event.is_null()) {
                    const auto hz=offset+event["carrier_offset_hz"].get<double>();
                    if(std::abs(hz)<=width/2) {
                        event["evidence_level"]="extended_candidate";
                        const auto sample=(i*hop+4)*double(rate)/channelRate-delay;
                        event["first_observed_sample"]=first+static_cast<std::uint64_t>(std::max(0.,std::round(sample)));
                        event["center_frequency_hz"]=center+hz;
                        results.push_back(std::move(event));
                    }
                }
            }
            i=end;
        }
    }
    return results;
}
Json detectPsk(std::span<const std::uint8_t> bytes,SampleFormat format,std::uint32_t rate,
               double center,double width,std::uint64_t first,PskSelection selection) {
    return detectSnapshot(bytes,format,rate,center,width,first,{selection,false});
}
}
