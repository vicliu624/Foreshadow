#include "plugins/wspr/core/fec.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value) {
    if (!value)
        throw std::runtime_error("WSPR FEC reference mismatch");
}
std::array<std::uint8_t, 162> encode(const std::array<std::uint8_t, 11> &bytes) {
    std::array<std::uint8_t, 162> result{};
    std::uint32_t state = 0;
    for (unsigned bit = 0; bit < 81; ++bit) {
        state = (state << 1) | ((bytes[bit / 8] >> (7 - bit % 8)) & 1u);
        result[2 * bit] = std::popcount(state & 0xf2d05351u) % 2 ? 255 : 0;
        result[2 * bit + 1] = std::popcount(state & 0xe4613c47u) % 2 ? 255 : 0;
    }
    return result;
}
} // namespace
int main() {
    using namespace signalscope::wspr;
    const std::array<std::uint8_t, 11> expected{0xf7, 0x0d, 0xdd, 0x7b, 0x39, 0xd9, 0x40, 0, 0, 0, 0};
    const auto reference = encode(expected);
    const auto clean = decode_fec(reference, 1000);
    require(clean.state == FecState::decoded && clean.bytes == expected && clean.cycles > 0);
    auto noisy = reference;
    for (auto &value : noisy)
        value = value ? 210 : 45;
    noisy[17] = 255 - noisy[17];
    noisy[94] = 255 - noisy[94];
    const auto corrected = decode_fec(noisy, 10000);
    require(corrected.state == FecState::decoded && corrected.bytes == expected);
    bool rejected = false;
    try {
        decode_fec(reference, 0);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected);
    std::cout << "WSPR FEC independent encoding, soft-error and budget checks passed\n";
}
