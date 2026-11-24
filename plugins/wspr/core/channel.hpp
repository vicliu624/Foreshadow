#pragma once
#include "fec.hpp"
#include "message.hpp"
namespace signalscope::wspr {
using TonePowers = std::array<std::array<double, 4>, channel_symbols>;
struct ChannelFrame {
    Message message;
    FecResult fec;
    double synchronization = 0;
    unsigned hard_symbol_mismatches = 0;
};
std::optional<ChannelFrame> decode_channel(const TonePowers &, unsigned cycles_per_bit = 10000);
} // namespace signalscope::wspr
