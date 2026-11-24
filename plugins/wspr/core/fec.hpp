#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace signalscope::wspr {
inline constexpr unsigned channel_symbols = 162, payload_bits = 50, tail_bits = 31;
enum class FecState { decoded, exhausted };
struct FecResult {
    FecState state = FecState::exhausted;
    std::array<std::uint8_t, 11> bytes{};
    unsigned cycles = 0, progress = 0;
};
// Deinterleaved soft bits: 0 strongly favors zero, 255 strongly favors one.
// Successful FEC is not a checksum or a semantically validated message.
FecResult decode_fec(std::span<const std::uint8_t, channel_symbols>, unsigned cycles_per_bit);
} // namespace signalscope::wspr
