#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace signalscope::dtmb {
// GB 20600-2006, 4.6.2, figures 11/12/14. D1 is bit zero,
// the last register is the output, and feedback enters D1.
inline std::vector<float> pnSequence(unsigned order,std::uint32_t taps,std::uint32_t state,std::size_t count) {
    std::vector<float> result;result.reserve(count);
    const auto mask=(1u<<order)-1;
    for(std::size_t i=0;i<count;++i) {
        result.push_back((state>>(order-1))&1?-1.f:1.f);
        state=((state<<1)&mask)|(std::popcount(state&taps)&1);
    }
    return result;
}
struct HeaderMode {
    unsigned symbols,period,order;
    std::uint32_t taps,initial;
    unsigned match_symbols;
};
inline constexpr HeaderMode headerModes[]{
    {420,255,8,0xb1,0xb0,128},
    {595,1023,10,0x204,0x200,256},
    {945,511,9,0x1c2,0x1f7,256},
};
inline constexpr unsigned bodySymbols=3780,symbolRate=7560000;
}
