#include "fec.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
int foreshadow_wspr_fano(unsigned *, unsigned *, unsigned *, unsigned char *, unsigned char *, unsigned,
                         int[2][256], int, unsigned);
}
namespace signalscope::wspr {
FecResult decode_fec(std::span<const std::uint8_t, channel_symbols> symbols, unsigned cycles_per_bit) {
    if (cycles_per_bit < 1 || cycles_per_bit > 100000)
        throw std::invalid_argument("Invalid WSPR FEC cycle budget");
    int metrics[2][256];
    for (int value = 0; value < 256; ++value) {
        const double p = (value + .5) / 256.;
        metrics[0][value] = int(std::lround(10 * (std::log2(2 * (1 - p)) - .45)));
        metrics[1][value] = int(std::lround(10 * (std::log2(2 * p) - .45)));
    }
    std::array<std::uint8_t, channel_symbols> input;
    std::copy(symbols.begin(), symbols.end(), input.begin());
    FecResult result;
    unsigned metric = 0;
    const auto status =
        foreshadow_wspr_fano(&metric, &result.cycles, &result.progress, result.bytes.data(), input.data(),
                             payload_bits + tail_bits, metrics, 60, cycles_per_bit);
    if (status == 0 && result.cycles && result.cycles <= cycles_per_bit * (payload_bits + tail_bits) + 1)
        result.state = FecState::decoded;
    return result;
}
} // namespace signalscope::wspr
