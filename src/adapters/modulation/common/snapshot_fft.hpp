#pragma once

#include "snapshot_detectors.hpp"
#include <kiss_fft.h>
#include <memory>

namespace signalscope::snapshot {
class Fft {
public:
    explicit Fft(std::size_t size,bool inverse=false):in_(size),out_(size),
        plan_(kiss_fft_alloc(static_cast<int>(size),inverse?1:0,nullptr,nullptr),kiss_fft_free) {
        if(!plan_)throw std::bad_alloc();
    }
    void set(std::size_t k,Complex sample){in_[k]={sample.real(),sample.imag()};}
    void run(){kiss_fft(plan_.get(),in_.data(),out_.data());}
    Complex value(std::size_t k) const {return {out_[k].r,out_[k].i};}
    float power(std::size_t k) const {const auto z=out_[k];return z.r*z.r+z.i*z.i;}
    float magnitude(std::size_t k) const {return std::hypot(out_[k].r,out_[k].i);}
private:
    std::vector<kiss_fft_cpx> in_,out_;
    std::unique_ptr<kiss_fft_state,decltype(&kiss_fft_free)> plan_;
};

inline double binFrequency(std::size_t bin,std::size_t size,double rate) {
    return (double(bin)-(bin>=size/2?double(size):0.))*rate/size;
}
}
