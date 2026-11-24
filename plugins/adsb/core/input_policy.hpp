#pragma once
#include <cmath>
#include <cstdint>
namespace signalscope::adsb {
inline constexpr double frequency_hz=1090000000.,passband_hz=2000000.;
inline constexpr std::uint32_t minimum_rate=2000000,maximum_rate=20000000;
inline bool supports_rate(std::uint32_t rate){return rate>=minimum_rate && rate<=maximum_rate;}
inline bool covers_band(double center,double span,std::uint32_t rate,double filter=0) {
    return supports_rate(rate) && std::isfinite(center) && std::isfinite(span) && span>=passband_hz &&
        std::abs(center-frequency_hz)+passband_hz/2<=span/2 &&
        (filter<=0 || std::abs(center-frequency_hz)+passband_hz/2<=filter/2);
}
}
