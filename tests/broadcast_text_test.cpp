#include "signalscope/backend/transport/broadcast_text.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace signalscope::transport;
namespace {void check(std::initializer_list<std::uint8_t> bytes,const std::string& expected,const std::string& fallback="ISO_6937") {
    const std::vector<std::uint8_t> input(bytes);const auto actual=decodeBroadcastText(input,fallback);
    if(!actual || *actual!=expected)throw std::runtime_error("Broadcast text mismatch with "+fallback+": expected "+expected+", got "+actual.value_or("<invalid>"));
}}
int main() {
    try {
        check({'C','G','T','N'},"CGTN");check({'c','a','f',0xc2,'e'},"caf\xc3\xa9");
        check({0xc1,'A',0xc2,' ',0xcf,'z'},"\xc3\x80\xc2\xb4\xc5\xbe");
        check({0xa4,'1'},"\xe2\x82\xac" "1");
        check({0x13,0xd6,0xd0,0xce,0xc4},"\xe4\xb8\xad\xe6\x96\x87");
        check({0xd6,0xd0,0xce,0xc4},"\xe4\xb8\xad\xe6\x96\x87","GB18030");
        check({0x15,0xf0,0x9f,0x93,0xa1},"\xf0\x9f\x93\xa1");
        check({0x11,0xd8,0x3d,0xdc,0xfb},"\xf0\x9f\x93\xbb");
        const std::vector<std::uint8_t> broken{0x15,0xc0,0xaf},truncated{0x11,0xd8},unknown{0x1f,0x01},accent{0xc2},invalidPair{0xc2,0xff};
        for(const auto& bytes:{broken,truncated,unknown,accent,invalidPair})if(decodeBroadcastText(bytes))throw std::runtime_error("Malformed or unsupported broadcast text was guessed");
        std::cout<<"Broadcast selectors, ISO6937 accents, Chinese profiles, non-BMP Unicode and invalid text passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
