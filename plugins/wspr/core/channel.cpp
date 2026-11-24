#include "channel.hpp"
#include "interleave.hpp"
#include "sync.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace signalscope::wspr {
std::optional<ChannelFrame> decode_channel(const TonePowers &powers, unsigned cycles_per_bit) {
    double signal = 0, total = 0;
    std::array<std::uint8_t, channel_symbols> soft{};
    for (unsigned n = 0; n < channel_symbols; ++n) {
        for (const auto p : powers[n])
            if (!std::isfinite(p) || p < 0)
                return {};
        const auto s = sync_bits[n];
        const double a = powers[n][s], b = powers[n][s + 2], other = powers[n][1 - s] + powers[n][3 - s];
        signal += a + b - other;
        total += a + b + other;
        soft[n] = std::uint8_t(std::clamp(std::lround(255 * (b + 1e-30) / (a + b + 2e-30)), 0L, 255L));
    }
    if (total <= 1e-25 || signal / total < .1)
        return {};
    ChannelFrame frame;
    frame.synchronization = signal / total;
    frame.fec = decode_fec(deinterleave(soft), cycles_per_bit);
    if (frame.fec.state != FecState::decoded)
        return {};
    const auto message = unpack_message(frame.fec.bytes);
    if (!message)
        return {};
    frame.message = *message;
    std::array<unsigned, 162> coded{};
    std::uint32_t state = 0;
    for (unsigned bit = 0; bit < 81; ++bit) {
        state = (state << 1) | ((frame.fec.bytes[bit / 8] >> (7 - bit % 8)) & 1u);
        coded[2 * bit] = std::popcount(state & 0xf2d05351u) % 2;
        coded[2 * bit + 1] = std::popcount(state & 0xe4613c47u) % 2;
    }
    unsigned position = 0;
    for (unsigned n = 0; n < 256; ++n) {
        unsigned reversed = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            reversed = (reversed << 1) | ((n >> bit) & 1u);
        if (reversed >= 162)
            continue;
        const auto tone = 2 * coded[position++] + sync_bits[reversed];
        const auto observed = unsigned(std::max_element(powers[reversed].begin(), powers[reversed].end()) -
                                       powers[reversed].begin());
        if (tone != observed)
            ++frame.hard_symbol_mismatches;
    }
    return frame;
}
} // namespace signalscope::wspr
