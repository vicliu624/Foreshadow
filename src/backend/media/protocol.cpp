#include "signalscope/backend/media/protocol.hpp"
#include <bit>

namespace signalscope::media {
namespace {
std::uint64_t read(std::span<const std::uint8_t> bytes,std::size_t at,unsigned length) {
    std::uint64_t result=0;for(unsigned i=0;i<length;++i)result|=std::uint64_t(bytes[at+i])<<(i*8);return result;
}
void write(Bytes& bytes,std::size_t at,std::uint64_t value,unsigned length){for(unsigned i=0;i<length;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(i*8));}
void validate(MessageType type,std::size_t metadata,std::size_t payload) {
    if(type<MessageType::capabilities || type>MessageType::ended)throw std::invalid_argument("media.unsupported_message");
    if(metadata>maximumMetadata || payload>maximumPayload ||
       (type==MessageType::audio && payload>1024*1024) ||
       (type!=MessageType::audio && type!=MessageType::video && payload))throw std::invalid_argument("media.message_budget");
}
}
Bytes encode(const Message& message) {
    if(!message.metadata.is_object())throw std::invalid_argument("media.invalid_metadata");
    const auto metadata=message.metadata.dump(-1,' ',false,Json::error_handler_t::replace);
    validate(message.type,metadata.size(),message.payload.size());
    Bytes bytes(headerBytes+metadata.size()+message.payload.size());
    std::copy_n("AMSM",4,bytes.begin());write(bytes,4,protocolVersion,2);write(bytes,6,static_cast<std::uint16_t>(message.type),2);
    write(bytes,8,metadata.size(),4);write(bytes,12,message.payload.size(),4);write(bytes,16,std::bit_cast<std::uint64_t>(message.pts_us),8);
    std::copy(metadata.begin(),metadata.end(),bytes.begin()+headerBytes);
    std::copy(message.payload.begin(),message.payload.end(),bytes.begin()+headerBytes+metadata.size());return bytes;
}
MessageParser::MessageParser(Sink sink):sink_(std::move(sink)){if(!sink_)throw std::invalid_argument("media.missing_sink");}
void MessageParser::feed(std::span<const std::uint8_t> bytes) {
    while(!bytes.empty()) {
        const auto count=std::min(bytes.size(),target_-pending_.size());
        pending_.insert(pending_.end(),bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count));bytes=bytes.subspan(count);
        if(pending_.size()!=target_)continue;
        if(target_==headerBytes) {
            if(!std::equal(pending_.begin(),pending_.begin()+4,"AMSM"))throw std::invalid_argument("media.invalid_magic");
            if(read(pending_,4,2)!=protocolVersion)throw std::invalid_argument("media.unsupported_protocol");
            const auto type=static_cast<MessageType>(read(pending_,6,2));const auto metadata=read(pending_,8,4),payload=read(pending_,12,4);
            validate(type,metadata,payload);target_=headerBytes+metadata+payload;
            if(target_>headerBytes)continue;
        }
        const auto metadata=static_cast<std::size_t>(read(pending_,8,4));
        const auto begin=pending_.begin()+headerBytes;
        auto parsed=Json::parse(begin,begin+static_cast<std::ptrdiff_t>(metadata));
        if(!parsed.is_object())throw std::invalid_argument("media.invalid_metadata");
        Message message{static_cast<MessageType>(read(pending_,6,2)),std::bit_cast<std::int64_t>(read(pending_,16,8)),std::move(parsed),
            Bytes(begin+static_cast<std::ptrdiff_t>(metadata),pending_.end())};
        pending_.clear();target_=headerBytes;sink_(std::move(message));
    }
}
void MessageParser::finish(){if(!pending_.empty())throw std::invalid_argument("media.truncated_message");}
}
