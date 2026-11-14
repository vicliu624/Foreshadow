#include "../common/snapshot_fft.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace signalscope::snapshot {
Complex phasePower(Complex value,int order) {
    const auto square=value*value;
    return order==2?square:square*square;
}

Json identifyPsk(std::span<const Complex> signal,double rate,PskSelection selection) {
    if(signal.size()<128)return {};
    const auto n=std::min<std::size_t>(signal.size(),65536);
    std::vector<float> amplitude(n);
    for(std::size_t k=0;k<n;++k)amplitude[k]=std::abs(signal[k]);
    auto sorted=amplitude;
    auto middle=sorted.begin()+n/2;
    std::nth_element(sorted.begin(),middle,sorted.end());
    const auto median=*middle;
    if(median<=1e-7f)return {};
    std::vector<Complex> unit(n),corrected(n);
    for(std::size_t k=0;k<n;++k)unit[k]=signal[k]/std::max(amplitude[k],1e-12f);
    const auto fftSize=std::bit_ceil(n*2);
    Fft fft(fftSize);
    for(const int order:{2,4}) {
        if(order==2 && !selection.bpsk)continue;
        if(order==4 && !selection.qpsk)continue;
        for(std::size_t k=0;k<n;++k) {
            const auto window=(.5-.5*std::cos(tau*k/(n-1)))*std::min(amplitude[k]/median,1.f);
            fft.set(k,phasePower(unit[k],order)*static_cast<float>(window));
        }
        fft.run();
        double total=0,maximum=0;
        std::size_t peak=0;
        for(std::size_t k=0;k<fftSize;++k) {
            const auto power=fft.power(k);
            total+=power;
            if(std::abs(binFrequency(k,fftSize,rate))<=std::min(channelWidth*.35*order,rate*.45) && power>maximum) {
                maximum=power;peak=k;
            }
        }
        if(total<=0 || maximum<.16*total)continue;
        const auto carrier=binFrequency(peak,fftSize,rate)/order;
        unsigned strong=0,stable=0;
        for(std::size_t k=1;k<n;++k) {
            if(std::min(amplitude[k-1],amplitude[k])<.65f*median)continue;
            ++strong;
            if(std::abs(std::arg(unit[k]*std::conj(unit[k-1]))*rate/tau-carrier)<1200)++stable;
        }
        if(!strong || stable<.65*strong)continue;
        for(std::size_t k=0;k<n;++k)
            corrected[k]=unit[k]*std::polar(1.f,static_cast<float>(-tau*carrier*k/rate));
        double best=0;
        for(const int baud:{1200,2400,4800,9600,19200}) {
            const auto period=rate/baud;
            const auto symbols=static_cast<std::size_t>((n-1)/period);
            if(period<4 || symbols<24)continue;
            for(int phase=0;phase<8;++phase) {
                std::vector<Complex> constellation;
                Complex moment{};
                for(std::size_t j=0;j<symbols;++j) {
                    const auto k=std::size_t(std::llround((j+phase/8.)*period));
                    if(amplitude[k]<.45f*median)continue;
                    constellation.push_back(corrected[k]);
                    moment+=phasePower(corrected[k],order);
                }
                if(constellation.size()<.8*symbols)continue;
                const auto coherence=std::abs(moment)/constellation.size();
                if(coherence<.83)continue;
                const auto rotation=std::polar(1.f,-std::arg(moment)/order);
                std::array<unsigned,4> counts{};
                unsigned changes=0;
                int last=-1;
                for(const auto sample:constellation) {
                    const auto phaseState=(int(std::lround(std::arg(sample*rotation)*order/tau))+order)%order;
                    ++counts[phaseState];
                    if(last>=0 && last!=phaseState)++changes;
                    last=phaseState;
                }
                if(*std::min_element(counts.begin(),counts.begin()+order)>=.12*constellation.size() &&
                   changes>=.2*constellation.size())best=std::max(best,double(coherence));
            }
        }
        if(best>0)return {{"modulation",order==2?"BPSK":"QPSK"},{"carrier_offset_hz",carrier},{"phase_coherence",best}};
    }
    return {};
}
}
