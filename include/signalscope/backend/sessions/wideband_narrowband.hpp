#pragma once
#include "signalscope/adapters/modulation/fsk.hpp"

namespace signalscope {
// Maintains overlapping, anti-aliased analysis channels across the source span.
// Events always use the acquisition frequency and sample-index coordinate system.
class WidebandNarrowband {
public:
    WidebandNarrowband(const Config&,double search_width,SampleFormat,NarrowbandSelection);
    ~WidebandNarrowband();
    void feed(std::span<const std::uint8_t>,std::uint64_t first,NarrowbandEvents&);
    void finish(NarrowbandEvents&);
    Json status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
