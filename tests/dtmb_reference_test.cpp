#include "src/adapters/modulation/dtmb/pn.hpp"
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
namespace {
void check(std::span<const float> pn,std::string_view hex) {
    for(std::size_t bit=0;bit<4*hex.size();++bit) {
        const auto character=hex[bit/4];const auto value=character<='9'?character-'0':character-'A'+10;
        if(pn[bit]!=(value&(8>>(bit%4))?-1.f:1.f))throw std::runtime_error("DTMB PN differs from the normative reference bits");
    }
}
}
int main() {
    try {
        using namespace signalscope::dtmb;
        // Independent published vectors: GB 20600-2006 appendices D/E,
        // signal-frame zero. Checking literal bits catches register direction,
        // reciprocal-polynomial and preamble placement mistakes.
        const auto a=pnSequence(8,0xb1,0xb0,420);
        check(a,"B0A5E9FEA1CF0D9A3DC7407C4A22D5C8C938109BCCEFCB2B69063");
        const auto c=pnSequence(9,0x1c2,0x1f7,945);
        check(c,"FB946DFF3259AD7E9A7C9CDC081A82531A292661E8F1233A432F2D8055B");
        // Figure 12 gives D1..D10 = 0000000001: output D10 first.
        const auto b=pnSequence(10,0x204,0x200,1023);
        check(b,"80249A6B");
        for(const auto& mode:headerModes) {
            const auto sequence=pnSequence(mode.order,mode.taps,mode.initial,mode.period*2);
            for(unsigned i=0;i<mode.period;++i)if(sequence[i]!=sequence[i+mode.period])throw std::runtime_error("DTMB PN period mismatch");
        }
        std::cout<<"DTMB normative PN vectors and all three sequence periods passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
