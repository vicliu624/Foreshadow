#pragma once
#include "plugins/research/recovery/lora/codec.hpp"
#include <numbers>

namespace independent_lora_truth {
using signalscope::recovery::lora::PacketOptions;
// Independent polynomial long division, rather than the receiver register
// recurrence. The two trailing payload octets are XORed into the remainder.
unsigned crc(const std::vector<unsigned>& bytes) {
    std::vector<bool> polynomial;for(std::size_t i=0;i+2<bytes.size();++i)for(int bit=7;bit>=0;--bit)polynomial.push_back((bytes[i]>>bit)&1);
    const auto original=polynomial.size();polynomial.resize(original+16);
    for(std::size_t i=0;i<original;++i)if(polynomial[i])for(const auto power:{0,4,11,16})polynomial[i+power]=!polynomial[i+power];
    unsigned remainder=0;for(auto i=original;i<polynomial.size();++i)remainder=(remainder<<1)|polynomial[i];
    if(bytes.size()>=2)remainder^=bytes[bytes.size()-2]<<8;if(!bytes.empty())remainder^=bytes.back();return remainder;
}
unsigned headerChecksum(unsigned a,unsigned b,unsigned c) {
    const auto bit=[](unsigned value,unsigned position){return (value>>position)&1;};
    const unsigned c4=bit(a,0)^bit(a,1)^bit(a,2)^bit(a,3);
    const unsigned c3=bit(a,3)^bit(b,3)^bit(b,2)^bit(b,1)^bit(c,0);
    const unsigned c2=bit(a,2)^bit(b,3)^bit(b,0)^bit(c,3)^bit(c,1);
    const unsigned c1=bit(a,1)^bit(b,2)^bit(b,0)^bit(c,2)^bit(c,1)^bit(c,0);
    const unsigned c0=bit(a,0)^bit(b,1)^bit(c,3)^bit(c,2)^bit(c,1)^bit(c,0);
    return (c4<<4)|(c3<<3)|(c2<<2)|(c1<<1)|c0;
}
// A transmit-side codebook and a forward column rotation. Neither calls a
// receive helper. A published complete symbol vector below anchors the bit
// conventions independently of this synthetic encoder.
constexpr std::array<unsigned,16> hamming{0x00,0xd1,0x72,0xa3,0xb4,0x65,0xc6,0x17,0xe8,0x39,0x9a,0x4b,0x5c,0x8d,0x2e,0xff};
constexpr std::array<unsigned,16> parity5{0,17,18,3,20,5,6,23,24,9,10,27,12,29,30,15};
struct Encoded {PacketOptions options;std::vector<unsigned> words,gray,symbols;};
unsigned fromGray(unsigned value){unsigned result=0;while(value){result^=value;value>>=1;}return result;}
unsigned modulate(unsigned gray,unsigned slot,const PacketOptions& options){const auto scale=slot<8 || options.low_data_rate?4u:1u;return (fromGray(gray)*scale+1)% (1u<<options.sf);}
Encoded encode(const std::vector<unsigned>& payload,PacketOptions options,bool corruptCrc=false,bool corruptHeader=false) {
    if(payload.size()>255)throw std::invalid_argument("Invalid transmit truth length");options.payload_bytes=static_cast<unsigned>(payload.size());Encoded result;result.options=options;std::vector<unsigned> bytes=payload;
    if(options.payload_crc){const auto check=crc(payload)^(corruptCrc?1:0);bytes.push_back(check&255);bytes.push_back(check>>8);}
    unsigned state=255;for(unsigned i=0;i<payload.size();++i){bytes[i]^=state;const auto next=((state>>7)^(state>>5)^(state>>4)^(state>>3))&1;state=((state<<1)|next)&255;}
    std::vector<unsigned> nibbles;if(options.explicit_header){const unsigned a=static_cast<unsigned>(payload.size()>>4),b=static_cast<unsigned>(payload.size()&15),c=options.coding_rate*2+options.payload_crc,check=headerChecksum(a,b,c)^(corruptHeader?1:0);nibbles={a,b,c,check>>4,check&15};}
    for(const auto byte:bytes){nibbles.push_back(byte&15);nibbles.push_back(byte>>4);}
    const unsigned initial=options.sf-2,columns=options.sf-2*options.low_data_rate;
    if(nibbles.size()<initial)nibbles.resize(initial,15);else while((nibbles.size()-initial)%columns)nibbles.push_back(15);
    for(unsigned first=0;first<nibbles.size();){const auto count=first?columns:initial,width=first?4+options.coding_rate:8;for(unsigned col=0;col<count;++col){const auto nibble=nibbles[first+col];result.words.push_back(width==5?parity5[nibble]:hamming[nibble]&((1u<<width)-1));}
        for(unsigned row=0;row<width;++row){unsigned value=0;for(unsigned position=0;position<count;++position)value|=((result.words[first+(position+row)%count]>>row)&1)<<position;result.gray.push_back(value);result.symbols.push_back(modulate(value,static_cast<unsigned>(result.symbols.size()),options));}first+=count;
    }return result;
}
void flip(Encoded& encoded,unsigned word,unsigned bit) {
    const auto firstColumns=encoded.options.sf-2,columns=encoded.options.sf-2*encoded.options.low_data_rate,width=4+encoded.options.coding_rate;
    const auto block=word<firstColumns?0:1+(word-firstColumns)/columns,column=word<firstColumns?word:(word-firstColumns)%columns;
    const auto symbol=block?8+(block-1)*width+bit:bit,count=block?columns:firstColumns,position=(column+count-bit%count)%count;
    if(symbol>=encoded.symbols.size())throw std::invalid_argument("Fault outside transmit packet");encoded.gray[symbol]^=1<<position;encoded.symbols[symbol]=modulate(encoded.gray[symbol],symbol,encoded.options);
}

inline std::shared_ptr<signalscope::SignalWindow> iq(const Encoded& tx,std::uint64_t origin,unsigned rate=500000) {
    const auto chips=1u<<tx.options.sf;const unsigned reference=32,data=reference+chips*25/4;
    auto source=std::make_shared<signalscope::SignalWindow>();source->stream={rate,433012000,signalscope::SampleFormat::cf32_le};source->source_id="independent-paired-lora-tx";source->recording_origin=origin;
    signalscope::SignalSegment segment;segment.first_sample=origin;segment.samples.resize(data+chips*tx.symbols.size());
    const auto chirp=[&](unsigned u,unsigned symbol){const auto time=double(u),size=double(chips);const auto cycles=-time/2+time*time/(2*size)+symbol*time/size-std::max(0.,time-(size-symbol));return std::polar(.6f,float(.31+2*std::numbers::pi*cycles));};
    for(unsigned i=0;i<chips;++i)segment.samples[reference+i]=chirp(i,0);
    for(unsigned slot=0;slot<tx.symbols.size();++slot)for(unsigned i=0;i<chips;++i)segment.samples[data+slot*chips+i]=chirp(i,tx.symbols[slot]);
    source->segments.push_back(std::move(segment));return source;
}
}
