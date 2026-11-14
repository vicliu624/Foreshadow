#pragma once
#include "symbol_sampler.hpp"
#include "dtmb/core.hpp"
#include <array>
#include <complex>

namespace signalscope::television::native {
struct ClockEstimate {
    std::uint64_t rateMilliHz=0;double ppm=0,score=0,contrast=0;bool locked=false;
};
namespace clock_detail {
struct Correlation {
    std::complex<double> cross=0;double a=0,b=0;
    double score() const{return a>0 && b>0?std::norm(cross)/(a*b):0;}
};
inline std::vector<std::size_t> positions(std::size_t range,std::size_t count,std::size_t margin) {
    count=std::min(range,count);std::vector<std::size_t> result;result.reserve(count);
    for(std::size_t i=0;i<count;++i) {
        const auto first=range*i/count,last=range*(i+1)/count;
        auto hash=std::uint64_t(i+1)*0x9e3779b97f4a7c15ULL;hash=(hash^(hash>>30))*0xbf58476d1ce4e5b9ULL;hash^=hash>>27;
        result.push_back(margin+first+hash%(last-first));
    }
    return result;
}
inline Correlation correlate(std::span<const float> source,std::span<const std::size_t> points,std::size_t lag,std::size_t origin=0) {
    Correlation result;
    for(const auto point:points) {
        const auto left=(point+origin)*2,right=(point+origin+lag)*2;
        const double ai=source[left],aq=source[left+1],bi=source[right],bq=source[right+1];
        result.cross+=std::complex<double>(ai*bi+aq*bq,ai*bq-aq*bi);result.a+=ai*ai+aq*aq;result.b+=bi*bi+bq*bq;
    }
    return result;
}
}
// All three GB20600 PN families repeat their header schedule every 945000
// symbols (125ms). Payload need not repeat: incoherent data averages away.
// Work is bounded by fixed observation counts, independent of an input rate's
// rational denominator. A flat CW correlation cannot establish a clock.
inline ClockEstimate estimateClock(std::span<const float> source,std::uint32_t inputRate,std::uint32_t outputRate,Workers& workers) {
    using namespace clock_detail;
    const auto& definition=dtmb::core::pn_definition(dtmb::core::PnMode::pn945);
    const auto period=definition.frame_symbols()*definition.frames_per_superframe;
    ClockEstimate result;result.rateMilliHz=std::uint64_t(inputRate)*1000;
    const double expected=double(inputRate)*period/outputRate;
    const auto center=std::size_t(std::llround(expected)),span=std::size_t(std::ceil(expected*200e-6))+3;
    constexpr std::size_t radius=16,margin=2*radius+4;
    const auto high=center+span,low=center-span;
    if(source.size()/2<high+2*margin+65536)return result;
    const auto range=source.size()/2-high-2*margin;
    const auto coarsePoints=positions(range,8192,margin),finePoints=positions(range,65536,margin);
    const auto stride=std::max<std::size_t>(1,inputRate/outputRate);
    std::vector<std::size_t> lags;for(auto lag=low;lag<=high;lag+=stride)lags.push_back(lag);lags.push_back(center);
    std::sort(lags.begin(),lags.end());lags.erase(std::unique(lags.begin(),lags.end()),lags.end());
    std::vector<double> scores(lags.size());workers.run(lags.size(),[&](auto first,auto last){for(auto i=first;i<last;++i)scores[i]=correlate(source,coarsePoints,lags[i]).score();},1);
    const auto peak=std::size_t(std::max_element(scores.begin(),scores.end())-scores.begin());
    auto sorted=scores;std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/2,sorted.end());
    result.score=std::sqrt(scores[peak]);result.contrast=result.score-std::sqrt(sorted[sorted.size()/2]);
    if(result.score<.05 || result.contrast<.03 || peak==0 || peak+1==lags.size())return result;
    auto best=lags[peak];double bestScore=0;
    for(auto lag=best-stride;lag<=lags[peak]+stride;++lag) {
        const auto score=correlate(source,finePoints,lag).score();if(score>bestScore){bestScore=score;best=lag;}
    }
    // Band-limited interpolation of the measured correlation avoids the bias
    // of a three-point parabola near a sinc-shaped peak. The local covariance
    // also normalizes the interpolated waveform's power.
    std::array<Correlation,2*radius+1> cross;
    std::array<double,2*radius+1> covariance;
    workers.run(cross.size(),[&](auto first,auto last){for(auto i=first;i<last;++i){cross[i]=correlate(source,finePoints,best+i-radius);covariance[i]=correlate(source,finePoints,i,best).cross.real();}},1);
    const auto evaluate=[&](double fraction) {
        std::array<double,2*radius+1> weights{};double sum=0;
        for(std::size_t k=0;k<weights.size();++k) {
            const auto t=double(k)-radius-fraction;
            if(std::abs(t)<radius) {
                const auto sinc=std::abs(t)<1e-12?1.:std::sin(std::numbers::pi*t)/(std::numbers::pi*t);
                weights[k]=sinc*(.42+.5*std::cos(std::numbers::pi*t/radius)+.08*std::cos(2*std::numbers::pi*t/radius));sum+=weights[k];
            }
        }
        for(auto& weight:weights)weight/=sum;
        std::complex<double> value=0;double power=0;
        for(std::size_t k=0;k<weights.size();++k){value+=weights[k]*cross[k].cross;for(std::size_t l=0;l<weights.size();++l)power+=weights[k]*weights[l]*covariance[k>l?k-l:l-k];}
        return power>0 && cross[radius].a>0?std::norm(value)/(power*cross[radius].a):0.;
    };
    double first=-.6,last=.6;
    constexpr double ratio=.618033988749894848;
    auto left=last-ratio*(last-first),right=first+ratio*(last-first),leftScore=evaluate(left),rightScore=evaluate(right);
    for(unsigned i=0;i<32;++i) {
        if(leftScore<rightScore){first=left;left=right;leftScore=rightScore;right=first+ratio*(last-first);rightScore=evaluate(right);}
        else {last=right;right=left;rightScore=leftScore;left=last-ratio*(last-first);leftScore=evaluate(left);}
    }
    const auto refined=best+(first+last)/2;result.ppm=(refined/expected-1)*1e6;
    if(std::abs(result.ppm)>200)return result;
    result.rateMilliHz=std::uint64_t(std::llround(refined*outputRate/period*1000));result.score=std::min(1.,std::sqrt(std::max(leftScore,rightScore)));result.locked=true;
    return result;
}
}
