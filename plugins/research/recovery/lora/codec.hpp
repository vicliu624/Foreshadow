#pragma once
#include "signalscope/backend/dsp/recovery.hpp"
#include <array>
#include <span>

namespace signalscope::recovery::lora {
// Provider-owned LoRa symbol coding. This is not a new host result type or a
// detector API; all evidence is ultimately expressed through RecoveryData.
struct PacketOptions {
    unsigned sf=11,coding_rate=1,payload_bytes=50;
    bool explicit_header=true,payload_crc=false,low_data_rate=false,correct_single_bit=true;
    std::uint64_t symbol_samples=0;
};
struct Word {
    std::uint16_t first_symbol=0,symbol_count=0;
    std::uint8_t received=0,known_mask=0,width=0,decoded=0,decoded_word=0,distance=0;
    bool nibble_known=false,word_known=false,corrected=false,reconstructed=false;
    RecoveryCheckState check=RecoveryCheckState::unknown;
};
struct Header {
    std::array<Word,12> words{};
    unsigned word_count=0,payload_bytes=0,coding_rate=0;
    bool payload_crc=false,parameters_known=false;
    RecoveryCheckState checksum=RecoveryCheckState::unknown,fec=RecoveryCheckState::unknown;
};
struct Octet {
    std::uint16_t low_word=0,high_word=UINT16_MAX;
    std::uint8_t value=0;bool known=false;
};
struct Packet {
    PacketOptions applied;Header header;
    std::vector<Word> words;std::vector<Octet> bytes;
    unsigned expected_symbols=0,observed_symbols=0;
    bool header_usable=false,truncated=false;
    RecoveryCheckState payload_fec=RecoveryCheckState::unknown,payload_checksum=RecoveryCheckState::unknown;
    std::uint16_t calculated_crc=0,received_crc=0;
    std::uint64_t operations=0;
    std::size_t workspace_payload_bytes() const;
};
Header inspect_header(std::span<const std::uint16_t>,unsigned sf,bool correct_single_bit=true);
Packet decode_symbols(std::span<const RecoveryValue>,const PacketOptions&,const RecoveryBudget& budget={},std::stop_token stop={});
RecoveryData packet_evidence(std::vector<RecoveryValue>,const PacketOptions&,const RecoveryBudget& budget={},std::stop_token stop={});
}
