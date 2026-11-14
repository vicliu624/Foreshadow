#pragma once

#include "signalscope/backend/iq/iq.hpp"

namespace signalscope {
struct PskSelection {
    bool bpsk=true;
    bool qpsk=true;
    bool any() const {return bpsk || qpsk;}
};

// Analyze one bounded wideband snapshot. Frequencies and sample indices refer
// to the original capture, including after channel filtering and decimation.
Json detectPsk(std::span<const std::uint8_t> bytes,SampleFormat format,
               std::uint32_t rate,double center,double width,std::uint64_t first,
               PskSelection selection={});
}
