#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <memory>
namespace signalscope {
struct SpectrumFrame {
    std::vector<float> bins_dbfs;
    std::vector<std::complex<float>> iq_preview;
    std::uint32_t rate=0;
    double frequency=0,power_dbfs=0;
    std::uint64_t first_sample=0,sequence=0;
    double search_width_hz=1'000'000;
    double filter_bandwidth_hz=0;
};
class Spectrometer {
public:
    explicit Spectrometer(SampleFormat format=SampleFormat::cu8,std::size_t fft_size=0);~Spectrometer();
    SpectrumFrame measure(std::span<const std::uint8_t>,std::uint64_t,std::uint32_t,double);
private: class Impl;std::unique_ptr<Impl> impl_;
};
}
