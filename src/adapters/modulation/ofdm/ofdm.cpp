#include "../common/snapshot_fft.hpp"
#include <algorithm>
#include <bit>

namespace signalscope::snapshot {
namespace {
using Complex64=std::complex<double>;

struct PrefixMatch {
    std::size_t lag=0,prefix=0,checked=0;
    double score=0;
};

void checkPrefix(std::span<const Complex> samples,std::size_t lag,std::size_t prefix,
                 std::span<const Complex64> products,PrefixMatch& best) {
    const auto period=lag+prefix;
    if(prefix<6 || products.size()/period<8)return;
    std::vector<Complex64> folded(period);
    std::vector<unsigned> counts(period);
    for(std::size_t i=0;i<products.size();++i){folded[i%period]+=products[i];++counts[i%period];}
    for(std::size_t i=0;i<period;++i)folded[i]/=counts[i];
    std::vector<double> coherence(period);
    Complex64 sum{};
    for(std::size_t k=0;k<prefix;++k)sum+=folded[k];
    for(std::size_t k=0;k<period;++k) {
        coherence[k]=std::abs(sum);
        sum+=folded[(k+prefix)%period]-folded[k];
    }
    const auto strongest=std::max_element(coherence.begin(),coherence.end());
    const auto phase=static_cast<std::size_t>(strongest-coherence.begin());
    if(*strongest<=0 || coherence[(phase+period/2)%period]>.38*(*strongest))return;
    Complex64 cross{};
    double norm=0;
    std::size_t checked=0,good=0;
    for(std::size_t start=phase;start+prefix<=products.size();start+=period) {
        Complex64 local{};
        double a=0,b=0;
        for(std::size_t k=start;k<start+prefix;++k) {
            local+=products[k];
            a+=std::norm(samples[k]);
            b+=std::norm(samples[k+lag]);
        }
        const auto scale=std::sqrt(a*b);
        cross+=local;norm+=scale;++checked;
        if(std::abs(local)>.55*scale)++good;
    }
    const auto score=std::abs(cross)/std::max(norm,1e-20);
    if(checked>=8 && good>=.7*checked && score>=.62 && score>best.score)
        best={lag,prefix,checked,score};
}

PrefixMatch identify(std::span<const Complex> samples,std::uint32_t rate) {
    // Keep the observation duration when the acquisition clock changes.
    samples=samples.first(std::min<std::size_t>(samples.size(),std::max<std::size_t>(65536,rate/25)));
    const auto maximum=std::min<std::size_t>(samples.size()/10,std::llround(rate*.004));
    const auto minimum=std::max<std::size_t>(24,std::llround(rate*.000020));
    if(maximum<=minimum)return {};
    const auto length=std::bit_ceil(2*samples.size());
    Fft forward(length),inverse(length,true);
    for(std::size_t i=0;i<samples.size();++i)forward.set(i,samples[i]);
    forward.run();
    double total=0,peak=0;
    for(std::size_t i=0;i<length;++i) {
        const auto power=forward.power(i);
        total+=power;peak=std::max(peak,double(power));
        inverse.set(i,{power,0});
    }
    if(total<=0 || peak>.12*total)return {};
    inverse.run();
    std::vector<double> correlation(maximum+1);
    for(std::size_t i=0;i<=maximum;++i)correlation[i]=inverse.magnitude(i)/total;
    std::vector<std::size_t> peaks;
    for(std::size_t lag=minimum;lag<maximum;++lag)
        if(correlation[lag]>=.035 && correlation[lag]>correlation[lag-1] && correlation[lag]>=correlation[lag+1])
            peaks.push_back(lag);
    const auto candidates=std::min<std::size_t>(peaks.size(),8);
    std::partial_sort(peaks.begin(),peaks.begin()+candidates,peaks.end(),
                      [&](auto a,auto b){return correlation[a]>correlation[b];});
    PrefixMatch best;
    for(std::size_t i=0;i<candidates;++i) {
        const auto lag=peaks[i];
        std::vector<double> nearby;
        for(auto k=lag-16;k<=std::min(maximum,lag+16);++k)
            if(k+3<lag || k>lag+3)nearby.push_back(correlation[k]);
        const auto middle=nearby.begin()+nearby.size()/2;
        std::nth_element(nearby.begin(),middle,nearby.end());
        if(correlation[lag]<2*(*middle))continue;
        std::vector<Complex64> products(samples.size()-lag);
        for(std::size_t k=0;k<products.size();++k)
            products[k]=Complex64(samples[k+lag])*std::conj(Complex64(samples[k]));
        for(const int divisor:{4,8,16,32})
            checkPrefix(samples,lag,std::llround(double(lag)/divisor),products,best);
    }
    return best;
}
}

void detectOfdm(std::span<const Complex> samples,std::uint32_t rate,std::uint64_t first,Json& results) {
    const auto hop=static_cast<std::size_t>(std::llround(rate*.001));
    std::vector<double> power(samples.size()/hop);
    if(power.size()<12)return;
    for(std::size_t i=0;i<power.size();++i) {
        for(const auto sample:samples.subspan(i*hop,hop))power[i]+=std::norm(sample);
        power[i]/=hop;
    }
    auto ordered=power;
    const auto floor=ordered.begin()+ordered.size()/5;
    std::nth_element(ordered.begin(),floor,ordered.end());
    const auto peak=*std::max_element(power.begin(),power.end());
    const auto threshold=std::max(5*std::max(*floor,1e-12),.002*peak);
    for(std::size_t begin=0;begin<power.size();) {
        if(power[begin]<=threshold){++begin;continue;}
        auto last=begin,active=begin;
        std::size_t hits=1;
        for(++active;active<power.size() && active-last<=5;++active)
            if(power[active]>threshold){last=active;++hits;}
        if(hits>=4) {
            const auto evidence=identify(samples.subspan(begin*hop,(last+1-begin)*hop),rate);
            if(evidence.checked)
                results.push_back({{"modulation","OFDM"},{"evidence_level","extended_candidate"},
                    {"first_observed_sample",first+begin*hop},{"end_sample_exclusive",first+(last+1)*hop},
                    {"useful_symbol_seconds",double(evidence.lag)/rate},{"prefix_seconds",double(evidence.prefix)/rate},
                    {"prefix_coherence",evidence.score},{"checked_symbols",evidence.checked}});
        }
        begin=active;
    }
}
}
