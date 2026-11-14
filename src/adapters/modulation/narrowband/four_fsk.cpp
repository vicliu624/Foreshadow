#include "../common/snapshot_detectors.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace signalscope::snapshot {
namespace {
double quantile(std::vector<double>& values,double fraction) {
    const auto point=values.begin()+static_cast<std::size_t>((values.size()-1)*fraction);
    std::nth_element(values.begin(),point,values.end());
    return *point;
}

std::size_t groupOf(double value,const std::array<double,4>& centers) {
    std::size_t group=0;
    while(group+1<centers.size() && value>(centers[group]+centers[group+1])/2)++group;
    return group;
}
}

Json identifyFourFsk(std::span<const Complex> signal,double rate) {
    if(signal.size()<128)return {};
    signal=signal.first(std::min<std::size_t>(signal.size(),65536));
    std::vector<double> amplitudes;
    amplitudes.reserve(signal.size());
    for(const auto sample:signal)amplitudes.push_back(std::abs(sample));
    auto ordered=amplitudes;
    const auto median=quantile(ordered,.5);
    if(median<=1e-7 || quantile(ordered,.2)<median*.5)return {};

    std::vector<double> frequency(signal.size()-1);
    for(std::size_t k=0;k<frequency.size();++k)
        frequency[k]=std::arg(signal[k+1]*std::conj(signal[k]))*rate/tau;
    ordered=frequency;
    std::array<double,4> centers{};
    for(std::size_t i=0;i<centers.size();++i)centers[i]=quantile(ordered,(i+.5)/centers.size());
    std::array<std::vector<double>,4> groups;
    for(int pass=0;pass<8;++pass) {
        for(auto& group:groups)group.clear();
        for(const auto value:frequency)groups[groupOf(value,centers)].push_back(value);
        for(std::size_t i=0;i<groups.size();++i) {
            if(groups[i].empty() || groups[i].size()<frequency.size()*.1)return {};
            centers[i]=quantile(groups[i],.5);
        }
    }
    const std::array gaps{centers[1]-centers[0],centers[2]-centers[1],centers[3]-centers[2]};
    const auto [small,large]=std::minmax_element(gaps.begin(),gaps.end());
    if(*small<800 || *small<.65*(*large) || centers.back()-centers.front()>channelWidth*.8)return {};

    std::array<unsigned,4> visits{};
    const auto dwell=std::max<std::size_t>(3,static_cast<std::size_t>(std::llround(rate*.00012)));
    std::size_t previous=groupOf(frequency.front(),centers),start=0;
    for(std::size_t k=1;k<frequency.size();++k) {
        const auto current=groupOf(frequency[k],centers);
        if(current==previous)continue;
        if(k-start>=dwell)++visits[previous];
        previous=current;start=k;
    }
    if(frequency.size()-start>=dwell)++visits[previous];
    if(*std::min_element(visits.begin(),visits.end())<3)return {};

    for(auto& group:groups)group.clear();
    for(const auto value:frequency) {
        const auto group=groupOf(value,centers);
        groups[group].push_back(std::abs(value-centers[group]));
    }
    for(auto& group:groups)
        if(group.empty() || quantile(group,.75)>*small*.22)return {};
    const auto midpoint=std::accumulate(centers.begin(),centers.end(),0.)/centers.size();
    for(auto& tone:centers)tone-=midpoint;
    return {{"modulation","4-FSK"},{"carrier_offset_hz",midpoint},{"tone_offsets_hz",centers},{"tone_visits",visits}};
}
}
