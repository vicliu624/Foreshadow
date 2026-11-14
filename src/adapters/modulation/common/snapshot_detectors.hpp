#pragma once

#include "signalscope/adapters/modulation/psk.hpp"
#include <numbers>

namespace signalscope::snapshot {
using Complex=std::complex<float>;
inline constexpr double tau=2*std::numbers::pi;
inline constexpr double channelWidth=40000;
Json identifyPsk(std::span<const Complex>,double rate,PskSelection);
Json identifyFourFsk(std::span<const Complex>,double rate);
void detectOfdm(std::span<const Complex>,std::uint32_t rate,std::uint64_t first,Json& results);
void detectCss(std::span<const Complex>,std::uint32_t rate,double center,double width,
               std::uint64_t first,Json& results);
}
