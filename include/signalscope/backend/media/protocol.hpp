#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <limits>

namespace signalscope::media {
inline constexpr unsigned protocolVersion=1;
enum class MessageType:std::uint16_t {capabilities=1,catalog=2,video=3,audio=4,status=5,error=6,ended=7};
struct Message {
    MessageType type;
    std::int64_t pts_us=std::numeric_limits<std::int64_t>::min();
    Json metadata=Json::object();
    Bytes payload;
};
inline constexpr std::size_t headerBytes=24,maximumMetadata=256*1024,maximumPayload=64*1024*1024;
Bytes encode(const Message&);
class MessageParser {
public:
    using Sink=std::function<void(Message)>;
    explicit MessageParser(Sink);
    void feed(std::span<const std::uint8_t>);
    void finish();
    std::size_t pendingBytes() const{return pending_.size();}
private:
    Sink sink_;Bytes pending_;std::size_t target_=headerBytes;
};
}
