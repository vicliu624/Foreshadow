#include "../common/snapshot_fft.hpp"
#include <algorithm>
#include <cmath>

namespace signalscope::snapshot {
namespace {
struct Sweep {
    std::uint64_t first,last;
    double center,span,slope;
};
struct Train {
    Sweep sweep;
    double period;
    std::size_t repeats,fft;
};

double median(std::vector<double>& values) {
    auto middle=values.begin()+values.size()/2;
    std::nth_element(values.begin(),middle,values.end());
    if(values.size()%2)return *middle;
    return (*middle+*std::max_element(values.begin(),middle))/2;
}

std::vector<Sweep> findSweeps(std::span<const Complex> samples,double rate,double center,std::size_t size) {
    const auto hop=size/4;
    const std::size_t points=size<=64?12:8;
    if(samples.size()<size+points*hop)return {};
    const auto frames=1+(samples.size()-size)/hop;
    const auto backgroundCount=std::min<std::size_t>(frames,384);
    Fft fft(size);
    std::vector<float> window(size);
    for(std::size_t k=0;k<size;++k)window[k]=static_cast<float>(.5-.5*std::cos(tau*k/(size-1)));
    const auto transform=[&](std::size_t index) {
        for(std::size_t k=0;k<size;++k)fft.set(k,samples[index*hop+k]*window[k]);
        fft.run();
    };
    std::vector<std::vector<double>> background(size);
    for(auto& bin:background)bin.reserve(backgroundCount);
    for(std::size_t i=0;i<backgroundCount;++i) {
        const auto begin=i*frames/backgroundCount;
        const auto end=(i+1)*frames/backgroundCount;
        transform(begin+(i*53+19)%(end-begin));
        for(std::size_t k=0;k<size;++k)background[k].push_back(fft.power(k));
    }
    std::vector<double> floor(size);
    for(std::size_t k=0;k<size;++k)floor[k]=median(background[k]);

    std::vector<double> ridge(frames);
    std::vector<bool> valid(frames);
    for(std::size_t i=0;i<frames;++i) {
        transform(i);
        std::size_t peak=0;
        double best=fft.power(0)-4*floor[0];
        for(std::size_t k=1;k<size;++k) {
            const auto score=fft.power(k)-4*floor[k];
            if(score>best){best=score;peak=k;}
        }
        const auto shifted=(peak+size/2)%size;
        if(shifted==0 || shifted==size-1)continue;
        const auto b=fft.power(peak);
        if(b<std::max(15*floor[peak],1e-10))continue;
        const auto logPower=[&](std::size_t k){return std::log(std::max(1e-20,double(fft.power(k%size))));};
        const auto a=logPower(peak+size-1),m=logPower(peak),c=logPower(peak+1);
        ridge[i]=shifted+std::clamp(.5*(a-c)/std::min(a-2*m+c,-1e-6),-.5,.5);
        valid[i]=true;
    }

    std::vector<Sweep> sweeps;
    std::vector<double> slopes;
    std::size_t first=0,last=0;
    const auto binHz=rate/size;
    const double midpoint=(points-1)/2.;
    const double variance=double(points)*(points*points-1)/12.;
    const auto flush=[&] {
        if(slopes.size()>=3) {
            const auto [low,high]=std::minmax(ridge[first],ridge[last+points-1]);
            const auto span=(high-low)*binHz;
            if(span>=40000)sweeps.push_back({first*hop+size/2,(last+points-1)*hop+size/2,
                center+((low+high)/2-size/2)*binHz,span,median(slopes)*binHz*rate/hop});
        }
        slopes.clear();
    };
    for(std::size_t i=0;i+points<=frames;++i) {
        double sum=0,covariance=0;
        bool good=true;
        for(std::size_t k=0;k<points;++k) {
            if(!valid[i+k]){good=false;break;}
            sum+=ridge[i+k];
            covariance+=ridge[i+k]*(k-midpoint);
        }
        const auto slope=covariance/variance;
        if(good && std::abs(slope)*(points-1)>=3) {
            for(std::size_t k=0;k<points;++k)
                if(std::abs(ridge[i+k]-sum/points-slope*(k-midpoint))>.65){good=false;break;}
        } else good=false;
        if(!good){flush();continue;}
        if(!slopes.empty() && (slope*slopes.back()<=0 ||
            std::abs(slope-slopes.back())>.45*std::max(std::abs(slope),std::abs(slopes.back()))))flush();
        if(slopes.empty())first=i;
        last=i;
        slopes.push_back(slope);
    }
    flush();
    return sweeps;
}

void groupSweeps(const std::vector<Sweep>& sweeps,std::size_t fft,std::vector<Train>& trains) {
    std::vector<Sweep> repeated;
    const auto flush=[&] {
        if(repeated.size()>=3) {
            std::vector<double> centers,spans,slopes,periods;
            for(std::size_t k=0;k<repeated.size();++k) {
                const auto& sweep=repeated[k];
                centers.push_back(sweep.center);spans.push_back(sweep.span);slopes.push_back(sweep.slope);
                if(k)periods.push_back(sweep.first-repeated[k-1].first);
            }
            trains.push_back({{repeated.front().first,repeated.back().last,median(centers),median(spans),median(slopes)},
                              median(periods),repeated.size(),fft});
        }
        repeated.clear();
    };
    for(const auto& sweep:sweeps) {
        if(!repeated.empty()) {
            const auto& first=repeated.front();
            const auto& previous=repeated.back();
            const auto period=sweep.first-previous.first;
            bool same=first.slope*sweep.slope>0 && std::abs(sweep.slope-first.slope)<=.15*std::abs(first.slope) &&
                std::abs(sweep.center-first.center)<=.18*first.span &&
                std::abs(sweep.span-first.span)<=.28*first.span &&
                period>0 && period<=2*(first.last-first.first);
            if(repeated.size()>1) {
                const auto expected=double(previous.first-first.first)/(repeated.size()-1);
                same=same && std::abs(period-expected)<=.18*expected;
            }
            if(!same)flush();
        }
        repeated.push_back(sweep);
    }
    flush();
}

bool sameTrain(const Train& a,const Train& b) {
    const auto begin=std::max(a.sweep.first,b.sweep.first),end=std::min(a.sweep.last,b.sweep.last);
    if(end<=begin)return false;
    const auto duration=std::min(a.sweep.last-a.sweep.first,b.sweep.last-b.sweep.first);
    return end-begin>=.5*duration &&
        std::abs(a.sweep.center-b.sweep.center)<=.2*std::max(a.sweep.span,b.sweep.span) &&
        std::abs(a.sweep.slope-b.sweep.slope)<=.15*std::abs(a.sweep.slope) &&
        std::abs(a.period-b.period)<=.18*a.period;
}
}

void detectCss(std::span<const Complex> samples,std::uint32_t rate,double center,double width,
               std::uint64_t first,Json& results) {
    const auto middle=std::clamp(static_cast<int>(std::lround(std::log2(rate*.00015))),6,10);
    std::vector<Train> candidates;
    for(const auto exponent:{middle-2,middle,middle+2}) {
        const std::size_t fft=std::size_t{1}<<exponent;
        groupSweeps(findSweeps(samples,rate,center,fft),fft,candidates);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const Train& a,const Train& b){
        if(a.repeats!=b.repeats)return a.repeats>b.repeats;
        return a.sweep.span>b.sweep.span;
    });
    std::vector<Train> kept;
    for(const auto& item:candidates) {
        if(std::abs(item.sweep.center-center)>width/2)continue;
        if(std::none_of(kept.begin(),kept.end(),[&](const Train& other){return sameTrain(item,other);}))
            kept.push_back(item);
    }
    std::sort(kept.begin(),kept.end(),[](const Train& a,const Train& b){return a.sweep.first<b.sweep.first;});
    for(const auto& item:kept)
        results.push_back({{"modulation","CSS"},{"evidence_level","extended_candidate"},
            {"first_observed_sample",first+item.sweep.first},{"last_observed_sample",first+item.sweep.last},
            {"center_frequency_hz",item.sweep.center},{"observed_span_hz",item.sweep.span},
            {"sweep_rate_hz_per_second",item.sweep.slope},{"repeat_interval_seconds",item.period/rate},
            {"repeated_chirps",item.repeats},{"stft_window_samples",item.fft}});
}
}
