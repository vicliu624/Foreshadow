#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
namespace signalscope::wspr {
enum class MessageKind { standard, compound, hashed };
struct Message {
    MessageKind kind = MessageKind::standard;
    std::uint64_t payload = 0;
    std::string callsign, locator;
    std::optional<unsigned> callsign_hash;
    int power_dbm = 0;
};
// Interprets source bits only. FEC/synchronization evidence is separate.
// A hash is never promoted to a known callsign without matching evidence.
std::optional<Message> unpack_message(const std::array<std::uint8_t, 11> &);
} // namespace signalscope::wspr
