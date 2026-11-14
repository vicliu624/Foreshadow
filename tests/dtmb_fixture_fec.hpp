#pragma once

// Offline transmitter/oracle code. It does not include or call the receiver's
// FEC, scrambler, interleaver, QAM or PN implementations.
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dtmb_fixture {
inline void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Polynomial127 {
    std::uint64_t low=0,high=0;
    static Polynomial127 hexadecimal(const std::string& text) {
        check(text.size()==32,"Invalid normative generator polynomial");Polynomial127 value;
        value.high=std::stoull(text.substr(0,16),nullptr,16);value.low=std::stoull(text.substr(16),nullptr,16);
        check(!(value.high>>63),"Generator polynomial exceeds 127 coefficients");return value;
    }
    bool bit(unsigned index) const {return index<64?(low>>index)&1:(high>>(index-64))&1;}
    void rotate() {
        const auto carry=high>>62;high=((high<<1)|(low>>63))&0x7fffffffffffffffULL;low=(low<<1)|carry;
    }
    Polynomial127& operator^=(Polynomial127 other) {low^=other.low;high^=other.high;return *this;}
};

class FecEncoder {
public:
    FecEncoder(const nlohmann::json& tables,unsigned rate):informationBlocks_(rate==1?24:rate==2?36:48),parityBlocks_(59-informationBlocks_) {
        check(rate>=1 && rate<=3,"Invalid fixture FEC rate");const auto key=std::to_string(informationBlocks_*127);
        const auto& matrix=tables.at("ldpc_generator_first_rows").at(key);
        check(matrix.size()==informationBlocks_,"Incomplete standard generator matrix");
        for(const auto& row:matrix) {
            check(row.size()==parityBlocks_,"Incomplete standard generator row");
            for(const auto& value:row)generator_.push_back(Polynomial127::hexadecimal(value.get<std::string>()));
        }
        checks_=tables.at("ldpc_check_circulants").at(key).get<std::vector<std::array<unsigned,3>>>();
        for(const auto& a:checks_)check(a[0]<parityBlocks_ && a[1]<59 && a[2]<127,"Invalid standard parity-check circulant");
    }
    unsigned payloadBytes() const {return informationBlocks_*127/762*94;}
    // Appendix A's printed hex values are polynomial representations. Serial
    // columns use coefficients u0..u126. Verify that ordering independently
    // against Appendix B, including the cyclic wrap, before using this oracle.
    void verifyGenerator() const {
        std::vector<std::uint8_t> message(informationBlocks_*127);
        for(unsigned block=0;block<informationBlocks_;++block)for(const auto shift:{0u,1u,126u}) {
            message[block*127+shift]=1;const auto parity=encodeParity(message);message[block*127+shift]=0;
            std::vector<std::uint8_t> syndrome(parityBlocks_*127);
            for(const auto& entry:checks_)for(unsigned row=0;row<127;++row) {
                const auto column=(row+entry[2])%127;
                const bool value=entry[1]<parityBlocks_?parity[entry[1]].bit(column):
                    entry[1]-parityBlocks_==block && column==shift;
                syndrome[entry[0]*127+row]^=value;
            }
            for(const auto bit:syndrome)check(!bit,"Independent standard G/H matrices are not orthogonal");
        }
    }
    std::vector<std::uint8_t> encode(std::span<const std::uint8_t> transport,unsigned resetBytes) const {
        check(resetBytes && transport.size()%payloadBytes()==0,"Fixture requires complete FEC payloads");
        std::vector<std::uint8_t> result,message;result.reserve(transport.size()/payloadBytes()*7488);
        std::uint16_t scrambler=0xa9;unsigned position=0;
        for(std::size_t first=0;first<transport.size();first+=payloadBytes()) {
            message.clear();message.reserve(informationBlocks_*127);
            for(unsigned start=0;start<payloadBytes();start+=94) {
                std::uint16_t remainder=0;
                for(unsigned byte=0;byte<94;++byte)for(int bit=7;bit>=0;--bit) {
                    if(position==resetBytes*8){position=0;scrambler=0xa9;}
                    const auto random=((scrambler>>13)^(scrambler>>14))&1;
                    scrambler=((scrambler<<1)|random)&0x7fff;++position;
                    const auto value=((transport[first+start+byte]>>bit)&1)^random;
                    message.push_back(static_cast<std::uint8_t>(value));
                    remainder=(remainder<<1)|value;if(remainder&1024)remainder^=1033;
                }
                for(unsigned bit=0;bit<10;++bit){remainder<<=1;if(remainder&1024)remainder^=1033;}
                for(int bit=9;bit>=0;--bit)message.push_back((remainder>>bit)&1);
            }
            const auto parity=encodeParity(message);
            for(unsigned bit=5;bit<parityBlocks_*127;++bit)result.push_back(parity[bit/127].bit(bit%127));
            result.insert(result.end(),message.begin(),message.end());
        }
        return result;
    }
private:
    std::vector<Polynomial127> encodeParity(std::span<const std::uint8_t> message) const {
        check(message.size()==informationBlocks_*127,"Invalid fixture LDPC message length");
        std::vector<Polynomial127> parity(parityBlocks_);
        for(unsigned block=0;block<informationBlocks_;++block) {
            std::vector<Polynomial127> rows(generator_.begin()+block*parityBlocks_,generator_.begin()+(block+1)*parityBlocks_);
            for(unsigned bit=0;bit<127;++bit) {
                if(message[block*127+bit])for(unsigned column=0;column<parityBlocks_;++column)parity[column]^=rows[column];
                for(auto& row:rows)row.rotate();
            }
        }
        return parity;
    }
    unsigned informationBlocks_,parityBlocks_;
    std::vector<Polynomial127> generator_;
    std::vector<std::array<unsigned,3>> checks_;
};
}
