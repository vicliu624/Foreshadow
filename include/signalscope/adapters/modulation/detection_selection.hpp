#pragma once

#include <array>

namespace signalscope {
struct NarrowbandSelection {
    bool fsk=true;
    bool ook=true;
    bool ask=true;
    bool broadcast_fm=true;

    constexpr bool any() const {return fsk || ook || ask || broadcast_fm;}
};

struct NarrowbandOption {
    const char* id;
    const char* label;
    bool NarrowbandSelection::* enabled;
};

inline constexpr std::array narrowband_options{
    NarrowbandOption{"fsk","2-FSK / GFSK",&NarrowbandSelection::fsk},
    NarrowbandOption{"ook","OOK",&NarrowbandSelection::ook},
    NarrowbandOption{"ask","ASK",&NarrowbandSelection::ask},
    NarrowbandOption{"fm","Broadcast FM",&NarrowbandSelection::broadcast_fm},
};
}
