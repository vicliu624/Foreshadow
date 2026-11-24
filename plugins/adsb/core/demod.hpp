#pragma once
#include "codec.hpp"
#include <complex>
#include <functional>
#include <vector>
namespace signalscope::adsb {
struct Frame {Message bytes;Fields fields;std::uint64_t first_sample;};
// Streaming envelope PPM acquisition. State contains at most one frame of
// history; caller owns input continuity and executes this on its DSP worker.
class Demodulator {
public:
    explicit Demodulator(std::uint32_t rate);
    void reset();
    void feed(std::span<const std::complex<float>>,std::uint64_t first,bool discontinuous,
              const std::function<void(const Frame&)>&);
private:
    std::uint32_t rate_;
    std::size_t frame_samples_;
    std::vector<float> power_;
    std::size_t head_=0;
    std::uint64_t first_=0,next_=0;
    bool initialized_=false;
};
}
