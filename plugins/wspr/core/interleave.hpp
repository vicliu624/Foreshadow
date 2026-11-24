#pragma once
#include "fec.hpp"
namespace signalscope::wspr {
inline std::array<std::uint8_t, channel_symbols>
deinterleave(std::span<const std::uint8_t, channel_symbols> input) {
    std::array<std::uint8_t, channel_symbols> output{};
    unsigned position = 0;
    for (unsigned index = 0; index < 256; ++index) {
        unsigned reversed = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            reversed = (reversed << 1) | ((index >> bit) & 1u);
        if (reversed < channel_symbols)
            output[position++] = input[reversed];
    }
    return output;
}
} // namespace signalscope::wspr
