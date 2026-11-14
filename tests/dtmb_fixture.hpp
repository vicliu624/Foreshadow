#pragma once
// Independent framing source, directly following the GB 20600 register
// diagrams and literal frame-zero/initial-state tables. No detector PN helper
// or detection thresholds are used. Random bodies test framing, not SI/FEC/TS.
#include <kiss_fft.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

namespace dtmb_fixture {
using Complex=std::complex<float>;
constexpr double rate=7560000,tau=2*std::numbers::pi;
struct Options {
    unsigned header=420,body=3780,source_rate=10000000;
    double offset=37000,snr=12,timing=127.37,echo=0;
    int period_error=0;
    bool one_header=false,wrong_pn=false,continuous_pn=false;
};
inline std::vector<Complex> header(unsigned mode,unsigned frame,bool wrong=false) {
    constexpr unsigned states420[]{0xb0,0x61,0xd8,0xc2,0xec,0x85,0xf6,0x0a,0xfb,0x15,0x7d,0x2a,0x3e,0x54,0x1f,0xa8};
    constexpr unsigned states945[]{0x1f7,0x1ee,0xfb,0x1dc,0x7d,0x1b9,0x3e,0x172,0x11f,0xe5,0x18f,0x1ca,0xc7,0x194,0x163,0x128};
    const auto order=mode==420?8u:mode==945?9u:10u;
    const auto state=mode==420?states420[frame%16]:mode==945?states945[frame%16]:512u;
    std::vector<unsigned> registers(order);for(unsigned i=0;i<order;++i)registers[i]=(state>>i)&1;
    std::vector<Complex> result;
    for(unsigned i=0;i<mode;++i) {
        const auto sign=registers.back()?-1.f:1.f;result.push_back(Complex(sign,sign)/std::sqrt(2.f));
        const auto feedback=wrong?(registers[0]^registers.back()):mode==420?(registers[0]^registers[4]^registers[5]^registers[7]):
            mode==945?(registers[1]^registers[6]^registers[7]^registers[8]):(registers[2]^registers[9]);
        for(unsigned j=order-1;j>0;--j)registers[j]=registers[j-1];registers[0]=feedback;
    }
    return result;
}
inline double rootRaisedCosine(double x) {
    constexpr double alpha=.05,pi=std::numbers::pi;
    if(std::abs(x)<1e-10)return 1+alpha*(4/pi-1);
    if(std::abs(std::abs(x)-1/(4*alpha))<1e-8)return alpha/std::sqrt(2.)*((1+2/pi)*std::sin(pi/(4*alpha))+(1-2/pi)*std::cos(pi/(4*alpha)));
    return (std::sin(pi*x*(1-alpha))+4*alpha*x*std::cos(pi*x*(1+alpha)))/(pi*x*(1-16*alpha*alpha*x*x));
}
inline std::vector<Complex> make(Options options={}) {
    std::mt19937 random(112026+options.header);std::normal_distribution<float> normal(0,1);
    std::vector<Complex> symbols;
    auto inverse=kiss_fft_alloc(3780,1,nullptr,nullptr);if(!inverse)throw std::bad_alloc();
    std::vector<kiss_fft_cpx> bins(3780),output(3780);
    for(unsigned frame=0;frame<32;++frame) {
        auto prefix=header(options.header,frame,options.wrong_pn);
        for(unsigned i=0;i<3780;++i)bins[i]={(random()&1?1.f:-1.f)/std::sqrt(2.f),(random()&1?1.f:-1.f)/std::sqrt(2.f)};
        if(options.body==3780)kiss_fft(inverse,bins.data(),output.data());
        std::vector<Complex> body(3780);
        for(unsigned i=0;i<3780;++i)body[i]=options.body==3780?Complex(output[i].r,output[i].i)/std::sqrt(3780.f):Complex(bins[i].r,bins[i].i);
        if(options.one_header && frame!=3)for(auto& value:prefix)value=Complex(normal(random),normal(random))/std::sqrt(2.f);
        for(auto value:prefix)symbols.push_back(value*(options.header==595?1.f:std::sqrt(2.f)));
        symbols.insert(symbols.end(),body.begin(),body.end());
        if(options.period_error>0)symbols.insert(symbols.end(),options.period_error,{});
    }
    kiss_fft_free(inverse);
    if(options.continuous_pn) {
        const auto period=header(420,0);
        for(std::size_t i=0;i<symbols.size();++i)symbols[i]=period[i%255];
    }
    const auto count=static_cast<std::size_t>(options.source_rate*.016);
    std::vector<Complex> result(count);
    // Evaluate the pulse at source times directly. This transmitter does not
    // reuse the receiver's polyphase interpolation or its timing search.
    for(std::size_t i=0;i<count;++i) {
        const auto position=double(i)*rate/options.source_rate+options.timing;
        const auto whole=static_cast<std::ptrdiff_t>(std::floor(position));Complex value{},echo{};
        for(int tap=-24;tap<=24;++tap) {
            const auto index=whole+tap;
            if(index>=0 && index<static_cast<std::ptrdiff_t>(symbols.size())) {
                value+=symbols[index]*static_cast<float>(rootRaisedCosine(position-index));
                if(options.echo)echo+=symbols[index]*static_cast<float>(rootRaisedCosine(position-index-2.3));
            }
        }
        value+=echo*std::polar(static_cast<float>(options.echo),.7f);
        value*=std::polar(1.f,static_cast<float>(std::remainder(tau*options.offset*i/options.source_rate,tau)));
        result[i]=(value+Complex(normal(random),normal(random))*static_cast<float>(std::pow(10.,-options.snr/20)/std::sqrt(2.)))*.12f;
    }
    return result;
}
}
