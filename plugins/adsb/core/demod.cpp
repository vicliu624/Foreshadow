#include "demod.hpp"
#include "input_policy.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace signalscope::adsb {
Demodulator::Demodulator(std::uint32_t rate):rate_(rate),frame_samples_((std::uint64_t(rate)*120+999999)/1000000) {
    if(!supports_rate(rate))throw std::invalid_argument("ADS-B input rate outside 2-20 MS/s");
    power_.reserve(frame_samples_+1);
}
void Demodulator::reset(){power_.clear();head_=0;initialized_=false;}
void Demodulator::feed(std::span<const std::complex<float>> samples,std::uint64_t first,bool discontinuous,const std::function<void(const Frame&)>& emit) {
    if(samples.size()>UINT64_MAX-first)throw std::overflow_error("ADS-B sample clock overflow");
    if(discontinuous || !initialized_ || first!=next_){reset();first_=first;initialized_=true;}
    next_=first+samples.size();
    // Small chunks bound workspace independently of acquisition callback size.
    for(const auto sample:samples) {
        const auto p=std::norm(sample);
        if(power_.size()<frame_samples_)power_.push_back(std::isfinite(p)?p:0);
        else {power_[head_]=std::isfinite(p)?p:0;head_=(head_+1)%frame_samples_;}
        if(power_.size()<frame_samples_)continue;
        bool found=false;
        for(const double phase:{0.,-.4,.4,-.2,.2}) {
        const auto half=[&](unsigned slot){
            const auto begin=std::min(power_.size(),std::size_t(std::max(0LL,std::llround(double(slot)*rate_/2000000+phase))));
            const auto end=std::min(power_.size(),std::size_t(std::max(0LL,std::llround(double(slot+1)*rate_/2000000+phase))));
            double total=0;for(auto i=begin;i<end;++i)total+=power_[(head_+i)%frame_samples_];return total/std::max<std::size_t>(1,end-begin);
        };
        // Necessary condition of weakest-pulse > 2 * strongest quiet slot.
        // Reject background before integrating all sixteen preamble slots.
        if(half(0)<=2*half(1))continue;
        double low=0,high=0;double weakest=1e30,strongestLow=0;
        for(unsigned slot=0;slot<16;++slot){const auto value=half(slot);if(slot==0 || slot==2 || slot==7 || slot==9){high+=value;weakest=std::min(weakest,value);}else{low+=value;strongestLow=std::max(strongestLow,value);}}
        if(high>1e-10 && weakest>strongestLow*2 && high/4>low/12*4) {
            Message bytes{};bool certain=true;
            for(unsigned bit=0;bit<112;++bit){const auto a=half(16+bit*2),b=half(17+bit*2);
                if(std::max(a,b)<=std::min(a,b)*1.2 || std::max(a,b)<weakest*.1){certain=false;break;}
                if(a>b)bytes[bit/8]|=std::uint8_t(1u<<(7-bit%8));
            }
            if(certain)if(auto fields=decode(bytes)){emit({bytes,std::move(*fields),first_});found=true;}
        }
        if(found)break;
        }
        if(found){first_+=power_.size();power_.clear();head_=0;}
        else ++first_;
    }
}
}
