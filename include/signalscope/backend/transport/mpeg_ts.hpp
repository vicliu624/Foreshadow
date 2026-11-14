#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <memory>

namespace signalscope::transport {
inline constexpr std::size_t tsPacketBytes=188;
std::uint32_t mpegSectionCrc(std::span<const std::uint8_t>);
// Extract a complete, contiguous run of 188-byte packets from a bounded cache.
// No elementary-stream or payload validity is inferred from packet framing.
Bytes completePackets(std::span<const std::uint8_t>);
// Broadcast character encodings are supplied by their text codec. Raw DVB
// text remains available when no decoder is installed; it is never guessed
// to be UTF-8 or interpreted using the machine's current code page.
using BroadcastTextDecoder=std::function<std::string(std::span<const std::uint8_t>)>;
class MpegTsCatalog {
public:
    // Single-threaded stream state. Its owner publishes snapshots and must
    // serialize feed/finish/discontinuity; this parser never waits for media.
    explicit MpegTsCatalog(BroadcastTextDecoder={});
    ~MpegTsCatalog();
    MpegTsCatalog(const MpegTsCatalog&)=delete;
    MpegTsCatalog& operator=(const MpegTsCatalog&)=delete;
    void feed(std::span<const std::uint8_t>);
    void finish();
    void discontinuity();
    Json snapshot() const;
    std::uint64_t revision() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
