#include "codec.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace signalscope::recovery::lora {
namespace {
constexpr unsigned maximumSymbols=1024,maximumWords=532;
struct Work {
    std::uint64_t used=0,limit;std::stop_token stop;
    void add(std::uint64_t count){if(stop.stop_requested())throw std::runtime_error("LoRa decoding canceled");if(count>limit-used)throw std::length_error("LoRa decoding operation limit exceeded");used+=count;}
};
void options(const PacketOptions& v){if(v.sf<7 || v.sf>12 || v.coding_rate<1 || v.coding_rate>4 || v.payload_bytes>255)throw std::invalid_argument("Invalid LoRa packet coding options");}
unsigned mapped(unsigned symbol,unsigned sf,bool reduced){const auto value=reduced?symbol/4:(symbol+(1u<<sf)-1)% (1u<<sf);return value^(value>>1);}
// These are parity equations of the shortened systematic binary code, not a
// table of expected air payloads. CR4/5 and CR4/6 detect errors; only CR4/7 and
// CR4/8 have the distance needed to correct one fully observed bit.
unsigned encoded(unsigned value,unsigned width) {
    const auto parity=[&](unsigned mask){return std::popcount(value&mask)&1;};
    if(width==5)return value|(parity(15)<<4);
    return value|(parity(7)<<4)|(parity(14)<<5)|(width>=7?parity(11)<<6:0)|(width>=8?parity(13)<<7:0);
}
Word decide(Word word,bool correct,Work& work) {
    const auto full=(1u<<word.width)-1;
    const auto erasures=word.width-std::popcount(static_cast<unsigned>(word.known_mask));
    unsigned distance=9,best=0,ties=0;
    work.add(16);
    for(unsigned n=0;n<16;++n){const auto mismatch=static_cast<unsigned>(std::popcount((encoded(n,word.width)^word.received)&word.known_mask));if(mismatch<distance){distance=mismatch;best=n;ties=1;}else if(mismatch==distance)++ties;}
    word.distance=static_cast<std::uint8_t>(distance);word.decoded=word.received&15;word.decoded_word=word.received;
    const unsigned minimumDistance=word.width>=7?word.width-4:2;
    const bool accepted=ties==1 && (distance==0 || (correct && word.width>=7)) &&
        (erasures==0 || correct) && 2*distance+erasures<minimumDistance;
    word.nibble_known=(word.known_mask&15)==15;word.word_known=word.known_mask==full;
    if(accepted){word.decoded=static_cast<std::uint8_t>(best);word.decoded_word=static_cast<std::uint8_t>(encoded(best,word.width));word.corrected=distance>0;word.reconstructed=erasures>0;word.nibble_known=word.word_known=true;word.check=RecoveryCheckState::pass;}
    else word.check=word.known_mask==full && distance>0?RecoveryCheckState::fail:RecoveryCheckState::unknown;
    return word;
}
std::array<Word,12> block(std::span<const RecoveryValue> symbols,unsigned first,unsigned width,unsigned columns,unsigned sf,bool reduced,bool correct,Work& work) {
    std::array<Word,12> result{};const auto count=static_cast<unsigned>(std::min<std::size_t>(width,symbols.size()>first?symbols.size()-first:0));
    for(unsigned col=0;col<columns;++col){Word word;word.width=static_cast<std::uint8_t>(width);word.first_symbol=static_cast<std::uint16_t>(first);word.symbol_count=static_cast<std::uint16_t>(count);
        for(unsigned row=0;row<count;++row){const auto& symbol=symbols[first+row];if(symbol.value>=(1u<<sf))throw std::invalid_argument("LoRa symbol exceeds its spreading factor");const auto gray=mapped(static_cast<unsigned>(symbol.value),sf,reduced);const auto position=(col+columns-row%columns)%columns;word.received|=((gray>>position)&1)<<row;if(symbol.known)word.known_mask|=1<<row;work.add(1);}
        result[col]=decide(word,correct,work);
    }
    return result;
}
RecoveryCheckState combine(RecoveryCheckState a,RecoveryCheckState b){if(a==RecoveryCheckState::fail || b==RecoveryCheckState::fail)return RecoveryCheckState::fail;if(a==RecoveryCheckState::unknown || b==RecoveryCheckState::unknown)return RecoveryCheckState::unknown;return RecoveryCheckState::pass;}
Header header(const std::array<Word,12>& words,unsigned columns) {
    Header result;result.words=words;result.word_count=columns;result.fec=RecoveryCheckState::pass;
    bool known=true;for(unsigned i=0;i<5;++i){known=known && words[i].nibble_known;result.fec=combine(result.fec,words[i].check);}
    result.payload_bytes=words[0].decoded*16+words[1].decoded;result.coding_rate=words[2].decoded>>1;result.payload_crc=words[2].decoded&1;
    result.parameters_known=words[0].nibble_known && words[1].nibble_known && words[2].nibble_known && result.coding_rate>=1 && result.coding_rate<=4;
    if(known){const unsigned bits=(words[0].decoded<<8)|(words[1].decoded<<4)|words[2].decoded;constexpr std::array<unsigned,5> masks{0xf00,0x8e1,0x49a,0x257,0x12f};unsigned check=0;for(const auto mask:masks)check=(check<<1)|(std::popcount(bits&mask)&1);const auto received=static_cast<unsigned>(((words[3].decoded&1)<<4)|words[4].decoded);result.checksum=check==received?RecoveryCheckState::pass:RecoveryCheckState::fail;}
    return result;
}
unsigned symbolCount(const PacketOptions& v){const auto first=v.sf-2-(v.explicit_header?5:0),nibbles=2*v.payload_bytes+(v.payload_crc?4:0),columns=v.sf-2*v.low_data_rate;return 8+(nibbles>first?(nibbles-first+columns-1)/columns:0)*(4+v.coding_rate);}
std::uint8_t nextWhitening(std::uint8_t state){return static_cast<std::uint8_t>((state<<1)|((std::popcount(static_cast<unsigned>(state)&0xb8)&1)));}
std::uint16_t checksum(std::span<const Octet> bytes,unsigned size,Work& work){std::uint16_t value=0;for(unsigned i=0;i+2<size;++i){value^=std::uint16_t(bytes[i].value)<<8;for(unsigned bit=0;bit<8;++bit)value=static_cast<std::uint16_t>((value<<1)^((value&0x8000)?0x1021:0));work.add(8);}if(size>=2)value^=std::uint16_t(bytes[size-2].value)<<8;if(size)value^=bytes[size-1].value;return value;}
const char* issue(const Word& word){return word.check==RecoveryCheckState::fail?"issue.parity_failed":!word.nibble_known?"issue.fec_unknown":word.corrected?"issue.corrected":word.reconstructed?"issue.reconstructed":"";}
void append(RecoverySequence& sequence,unsigned value,bool known,std::string reason,std::vector<RecoveryLink> inputs,double metric=0){RecoveryValue item;item.value=value;item.known=known;item.issue_key=std::move(reason);item.inputs=std::move(inputs);item.metric=metric;sequence.values.push_back(std::move(item));}
}
std::size_t Packet::workspace_payload_bytes() const{return sizeof(Packet)+words.capacity()*sizeof(Word)+bytes.capacity()*sizeof(Octet)+3*sizeof(std::array<Word,12>)+sizeof(Work);}
Header inspect_header(std::span<const std::uint16_t> symbols,unsigned sf,bool correct) {
    if(sf<7 || sf>12 || symbols.size()!=8)throw std::invalid_argument("LoRa explicit header needs eight symbols and SF7..12");
    std::array<RecoveryValue,8> input;for(unsigned i=0;i<8;++i){input[i].value=symbols[i];input[i].known=true;}
    Work work{0,4096,{}};return header(block(input,0,8,sf-2,sf,true,correct,work),sf-2);
}
Packet decode_symbols(std::span<const RecoveryValue> symbols,const PacketOptions& requested,const RecoveryBudget& budget,std::stop_token stop) {
    options(requested);if(symbols.empty() || symbols.size()>maximumSymbols)throw std::invalid_argument("LoRa packet symbol limit exceeded");
    if(budget.workspace_payload_bytes<sizeof(Packet)+maximumWords*sizeof(Word)+257*sizeof(Octet)+3*sizeof(std::array<Word,12>)+sizeof(Work))throw std::length_error("LoRa packet coding workspace exceeds its budget");
    Work work{0,budget.maximum_operations,stop};work.add(1);Packet result;result.applied=requested;result.words.reserve(maximumWords);result.bytes.reserve(257);
    const auto initial=block(symbols,0,8,requested.sf-2,requested.sf,true,requested.correct_single_bit,work);
    if(requested.explicit_header)result.header=header(initial,requested.sf-2);
    else {result.header.words=initial;result.header.word_count=requested.sf-2;result.header.checksum=result.header.fec=RecoveryCheckState::not_applicable;}
    for(unsigned i=0;i<requested.sf-2;++i)result.words.push_back(initial[i]);
    result.header_usable=!requested.explicit_header || (result.header.parameters_known && result.header.checksum==RecoveryCheckState::pass);
    if(!result.header_usable){result.observed_symbols=static_cast<unsigned>(std::min<std::size_t>(8,symbols.size()));result.truncated=symbols.size()<8;result.operations=work.used;return result;}
    if(requested.explicit_header){result.applied.payload_bytes=result.header.payload_bytes;result.applied.payload_crc=result.header.payload_crc;result.applied.coding_rate=result.header.coding_rate;}
    result.expected_symbols=symbolCount(result.applied);result.observed_symbols=static_cast<unsigned>(std::min<std::size_t>(symbols.size(),result.expected_symbols));result.truncated=symbols.size()<result.expected_symbols;
    if(requested.symbol_samples)for(unsigned i=0;i<result.observed_symbols;++i)if(symbols[i].support.count<requested.symbol_samples)result.truncated=true;
    const auto width=4+result.applied.coding_rate,columns=requested.sf-2*requested.low_data_rate;
    for(unsigned first=8;first<result.observed_symbols;first+=width){const auto words=block(symbols,first,width,columns,requested.sf,requested.low_data_rate,requested.correct_single_bit,work);for(unsigned i=0;i<columns;++i){if(result.words.size()>=maximumWords)throw std::length_error("LoRa codeword count exceeds its budget");result.words.push_back(words[i]);}}
    const unsigned first=requested.explicit_header?5:0,total=result.applied.payload_bytes+(result.applied.payload_crc?2:0);std::uint8_t whitening=255;
    result.payload_fec=RecoveryCheckState::pass;
    for(unsigned offset=0;offset<total && first+offset*2<result.words.size();++offset){const auto lo=first+offset*2,hi=lo+1;Octet byte;byte.low_word=static_cast<std::uint16_t>(lo);byte.value=result.words[lo].decoded;byte.known=result.words[lo].nibble_known;result.payload_fec=combine(result.payload_fec,result.words[lo].check);
        if(hi<result.words.size()){byte.high_word=static_cast<std::uint16_t>(hi);byte.value|=result.words[hi].decoded<<4;byte.known=byte.known && result.words[hi].nibble_known;result.payload_fec=combine(result.payload_fec,result.words[hi].check);}else {byte.known=false;result.payload_fec=combine(result.payload_fec,RecoveryCheckState::unknown);}
        if(offset<result.applied.payload_bytes){byte.value^=whitening;whitening=nextWhitening(whitening);}result.bytes.push_back(byte);work.add(1);
    }
    if(result.bytes.size()<total){result.truncated=true;result.payload_fec=combine(result.payload_fec,RecoveryCheckState::unknown);}
    if(!result.applied.payload_crc)result.payload_checksum=RecoveryCheckState::absent;
    else if(!result.truncated && result.bytes.size()==total && std::all_of(result.bytes.begin(),result.bytes.end(),[](const auto& v){return v.known;})) {result.calculated_crc=checksum(result.bytes,result.applied.payload_bytes,work);result.received_crc=result.bytes[total-2].value|(std::uint16_t(result.bytes[total-1].value)<<8);result.payload_checksum=result.calculated_crc==result.received_crc?RecoveryCheckState::pass:RecoveryCheckState::fail;}
    result.operations=work.used;if(result.workspace_payload_bytes()>budget.workspace_payload_bytes)throw std::length_error("LoRa coding allocated beyond its workspace budget");return result;
}
RecoveryData packet_evidence(std::vector<RecoveryValue> symbols,const PacketOptions& requested,const RecoveryBudget& budget,std::stop_token stop) {
    const auto packet=decode_symbols(symbols,requested,budget,stop);const auto count=packet.observed_symbols;const auto words=packet.words.size(),bytes=packet.bytes.size();
    const auto values=2*count+4*words+bytes,links=count+5*words+bytes;
    constexpr std::size_t fixed=8192,perValue=sizeof(RecoveryValue)+sizeof(RecoveryLink)+48;
    const auto allowance=budget.result_payload_bytes>=fixed?(budget.result_payload_bytes-fixed)/perValue:0;
    if(values>budget.maximum_values || links>budget.maximum_links || values>allowance || symbols.capacity()>allowance-values || (bytes && !budget.maximum_frames))throw std::length_error("LoRa evidence exceeds its result budget");
    RecoveryData data;data.kind=RecoveryKind::digital;data.workspace_payload_bytes=packet.workspace_payload_bytes();data.operations=packet.operations;
    data.stages={{"lora-symbols","stage.symbols",{},Json::object(),Json::object(),Json::object()},
        {"lora-gray","stage.gray",{0},{{"first_block_reduction",4},{"payload_reduction",requested.low_data_rate?4:1}},Json::object(),Json::object()},
        {"lora-deinterleave","stage.deinterleave",{1},{{"first_block_columns",requested.sf-2},{"first_block_rows",8},{"payload_columns",requested.sf-2*requested.low_data_rate},{"payload_rows",4+packet.applied.coding_rate}},Json::object(),Json::object()},
        {"lora-fec","stage.fec",{2},{{"single_bit_correction",requested.correct_single_bit},{"decision","unique codeword within 2e+s < dmin; otherwise retain systematic candidate"}},Json::object(),Json::object()},
        {"lora-bytes","stage.bytes",{3},{{"nibble_order","low first"},{"whitening_polynomial","x8+x6+x5+x4+1"},{"whitening_initial",255},{"crc_bytes_whitened",false}},Json::object(),Json::object()},
        {"lora-frame","stage.frame",{3,4},{{"explicit_header",requested.explicit_header}},Json::object(),Json::object()}};
    symbols.resize(count);data.sequences={{"symbols","sequence.symbols",0,RecoveryValueKind::symbol,std::move(symbols)},
        {"gray","sequence.gray",1,RecoveryValueKind::symbol,{}},{"received-codewords","sequence.received",2,RecoveryValueKind::symbol,{}},
        {"known-code-bits","sequence.masks",2,RecoveryValueKind::symbol,{}},{"decoded-codewords","sequence.corrected",3,RecoveryValueKind::symbol,{}},
        {"nibbles","sequence.nibbles",3,RecoveryValueKind::symbol,{}},{"payload-bytes","sequence.bytes",4,RecoveryValueKind::byte,{}}};
    data.sequences[1].values.reserve(count);for(unsigned i=2;i<6;++i)data.sequences[i].values.reserve(words);data.sequences[6].values.reserve(bytes);
    for(unsigned i=0;i<count;++i){const auto& value=data.sequences[0].values[i];append(data.sequences[1],mapped(static_cast<unsigned>(value.value),requested.sf,i<8 || requested.low_data_rate),value.known,value.known?"":"issue.symbol_erasure",{{0,i,1}});}
    unsigned headerCorrections=0,payloadCorrections=0,headerDistance=0,payloadDistance=0,reconstructed=0;
    for(unsigned i=0;i<words;++i){const auto& word=packet.words[i];const bool observed=word.known_mask==((1u<<word.width)-1);append(data.sequences[2],word.received,observed,observed?"":"issue.word_erasure",{{1,word.first_symbol,word.symbol_count}});
        append(data.sequences[3],word.known_mask,true,"",{{1,word.first_symbol,word.symbol_count}});append(data.sequences[4],word.decoded_word,word.word_known,word.word_known?issue(word):"issue.fec_unknown",{{2,i,1},{3,i,1}},word.distance);append(data.sequences[5],word.decoded,word.nibble_known,issue(word),{{4,i,1}});
        if(requested.explicit_header && i<5){headerDistance+=word.distance;headerCorrections+=word.corrected;}else {payloadDistance+=word.distance;payloadCorrections+=word.corrected;}reconstructed+=word.reconstructed;
    }
    for(const auto& byte:packet.bytes){const auto n=byte.high_word==UINT16_MAX?1u:2u;append(data.sequences[6],byte.value,byte.known,byte.known?"":n==1?"issue.partial_byte":"issue.byte_erasure",{{5,byte.low_word,n}});}
    data.stages[3].features={{"header_observed_distance",headerDistance},{"payload_observed_distance",payloadDistance},{"header_corrected_codewords",headerCorrections},{"payload_corrected_codewords",payloadCorrections},{"erasure_reconstructions",reconstructed}};
    data.stages[5].features={{"header_usable",packet.header_usable},{"header_checksum_state",static_cast<unsigned>(packet.header.checksum)},{"header_fec_state",static_cast<unsigned>(packet.header.fec)},{"expected_symbols",packet.expected_symbols},{"observed_symbols",packet.observed_symbols},{"truncated",packet.truncated}};
    data.parameters={{"sf",requested.sf},{"explicit_header",requested.explicit_header},{"low_data_rate",requested.low_data_rate},{"correct_single_bit",requested.correct_single_bit},{"payload_bytes",packet.applied.payload_bytes},{"coding_rate",packet.applied.coding_rate},{"payload_crc",packet.applied.payload_crc}};
    data.method={{"id","lora-symbol-coding"},{"version",1},{"header_usable",packet.header_usable},{"synchronization","provided symbol decisions; not acquired by this coding stage"},{"classification","not performed"},{"validation","LoRa PHY checks only; application and device unknown"},{"operation_unit","codeword candidate, deinterleaved bit, packed byte or CRC bit"}};
    if(bytes){const auto known=std::all_of(packet.bytes.begin(),packet.bytes.end(),[](const auto& value){return value.known;});RecoveryFrame frame;frame.id="lora-phy";frame.label_key="frame.label";frame.validation_scope_key="frame.scope";frame.sequence=6;frame.count=static_cast<std::uint32_t>(bytes);
        const auto headerCheck=requested.explicit_header?packet.header.checksum:RecoveryCheckState::not_applicable,headerFec=requested.explicit_header?packet.header.fec:RecoveryCheckState::not_applicable;
        frame.state=packet.truncated?RecoveryFrameState::truncated:headerCheck==RecoveryCheckState::fail || headerFec==RecoveryCheckState::fail || packet.payload_checksum==RecoveryCheckState::fail?RecoveryFrameState::invalid:known && packet.payload_checksum==RecoveryCheckState::pass && packet.payload_fec==RecoveryCheckState::pass && (!requested.explicit_header || (headerCheck==RecoveryCheckState::pass && headerFec==RecoveryCheckState::pass))?RecoveryFrameState::validated:RecoveryFrameState::candidate;
        frame.checks={{"known-bytes","check.bytes",4,known?RecoveryCheckState::pass:RecoveryCheckState::unknown,true,Json::object()},
            {"header-fec","check.header_fec",3,headerFec,requested.explicit_header,{{"observed_distance",headerDistance},{"corrected_codewords",headerCorrections}}},
            {"header-checksum","check.header",5,headerCheck,requested.explicit_header,Json::object()},
            {"payload-fec","check.payload_fec",3,packet.payload_fec,true,{{"observed_distance",payloadDistance},{"corrected_codewords",payloadCorrections}}},
            {"payload-crc","check.crc",5,packet.payload_checksum,packet.applied.payload_crc,(packet.payload_checksum==RecoveryCheckState::pass || packet.payload_checksum==RecoveryCheckState::fail)?Json{{"calculated",packet.calculated_crc},{"received",packet.received_crc}}:Json::object()}};
        frame.fields={{"declared_payload_bytes",packet.applied.payload_bytes},{"coding_rate_denominator",4+packet.applied.coding_rate},{"phy_payload_crc_present",packet.applied.payload_crc},{"application_semantics",{{"label_key","field.application_unknown"}}}};data.frames.push_back(std::move(frame));
    }
    if(stop.stop_requested())throw std::runtime_error("LoRa evidence canceled");
    if(data.payload_bytes()>budget.result_payload_bytes)throw std::length_error("LoRa evidence allocated beyond its budget");
    return data;
}
}
