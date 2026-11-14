#include "signalscope/backend/iq/sample_format.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <vector>
using namespace signalscope;
namespace {
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
}
int main() {
    try {
        // Literal wire vectors are independent of the encoder implementation.
        struct Case {SampleFormat format;std::complex<float> input;std::vector<std::uint8_t> expected;};
        const std::array cases{
            Case{SampleFormat::cu8,{0,0},{128,128}},
            Case{SampleFormat::cu8,{-1,1},{0,255}},
            Case{SampleFormat::ci16_le,{-.5f,.25f},{0,192,0,32}},
            Case{SampleFormat::ci16_le,{-2,2},{0,128,255,127}},
            Case{SampleFormat::cf32_le,{-.5f,.25f},{0,0,0,191,0,0,128,62}},
        };
        for(const auto& test:cases) {
            std::vector<std::uint8_t> output(bytes_per_pair(test.format),0x71);write_iq_pair(output.data(),test.format,test.input);
            require(output==test.expected,"Sample codec emitted incorrect wire bytes");
        }
        for(const auto& codec:sample_codecs) {
            std::array<std::uint8_t,8> wire{};
            for(int n=-999;n<=999;++n) {
                const auto input=std::complex<float>(n/1000.f,-n/2000.f);write_iq_pair(wire.data(),codec.format,input);
                const auto actual=codec.decode(wire.data());
                const auto tolerance=codec.format==SampleFormat::cu8?1./128:codec.format==SampleFormat::ci16_le?1./32768:1e-7;
                require(std::abs(input-actual)<=tolerance,"Sample codec round trip lost normalized component amplitude");
            }
            bool rejected=false;try{write_iq_pair(wire.data(),codec.format,{std::numeric_limits<float>::quiet_NaN(),0});}
            catch(const std::invalid_argument&){rejected=true;}require(rejected,"Non-finite I/Q was encoded");
        }
        std::cout<<"Independent sample wire vectors, saturation, normalization and finite-value guards passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
