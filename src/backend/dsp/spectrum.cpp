#include "signalscope/backend/dsp/spectrum.hpp"
#include <kiss_fft.h>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
namespace signalscope {
class Spectrometer::Impl {
public:
    explicit Impl(SampleFormat input_format,std::size_t requested):format(input_format),fixed_size(requested) {
        if(requested && (requested<16 || requested>65536 || !std::has_single_bit(requested)))throw std::invalid_argument("Invalid spectrum FFT size");
    }
    void resize(std::size_t n){if(in.size()==n)return;kiss_fft_free(plan);plan=kiss_fft_alloc(static_cast<int>(n),0,nullptr,nullptr);if(!plan)throw std::bad_alloc();in.resize(n);out.resize(n);}
    ~Impl(){kiss_fft_free(plan);}
    kiss_fft_cfg plan=nullptr;std::vector<kiss_fft_cpx> in,out;std::uint64_t sequence=0;
    const SampleFormat format;
    const std::size_t fixed_size;
};
Spectrometer::Spectrometer(SampleFormat format,std::size_t size):impl_(std::make_unique<Impl>(format,size)){}
Spectrometer::~Spectrometer()=default;
SpectrumFrame Spectrometer::measure(std::span<const std::uint8_t> bytes,std::uint64_t first,std::uint32_t rate,double frequency) {
    SpectrumFrame frame;frame.rate=rate;frame.frequency=frequency;frame.first_sample=first;frame.sequence=++impl_->sequence;
    const auto stride=bytes_per_pair(impl_->format);
    const auto size=impl_->fixed_size?impl_->fixed_size:std::clamp<std::size_t>(std::bit_ceil(std::size_t(std::ceil(rate/2000.))),1024,8192);
    const auto count=std::min<std::size_t>(size,bytes.size()/stride);
    if(count<16) return frame;
    impl_->resize(size);
    double window_sum=0,power=0;
    for(std::size_t i=0;i<size;++i) {
        if(i<count) {
            const double window=.5-.5*std::cos(2*std::numbers::pi*double(i)/double(count-1));window_sum+=window;
            const auto* sample=bytes.data()+i*stride;
            const auto value=read_iq_pair(sample,impl_->format);
            const float re=value.real(),im=value.imag();
            impl_->in[i]={static_cast<float>(re*window),static_cast<float>(im*window)};power+=re*re+im*im;
            if(i<128)frame.iq_preview.emplace_back(re,im);
        } else impl_->in[i]={0,0};
    }
    kiss_fft(impl_->plan,impl_->in.data(),impl_->out.data());frame.bins_dbfs.resize(size);
    for(std::size_t i=0;i<size;++i) {
        const auto& bin=impl_->out[(i+size/2)%size];
        const double value=(double(bin.r)*bin.r+double(bin.i)*bin.i)/(window_sum*window_sum);
        frame.bins_dbfs[i]=static_cast<float>(10*std::log10(std::max(value,1e-14)));
    }
    frame.power_dbfs=10*std::log10(std::max(power/count,1e-14));return frame;
}
}
