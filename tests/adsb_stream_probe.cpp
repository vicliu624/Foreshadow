#include "plugins/adsb/core/demod.hpp"
#include <chrono>
#include <iostream>
#include <vector>
int main() {
    using namespace signalscope::adsb;constexpr unsigned rate=2000000,seconds=30,blockSize=20000;
    const std::string hex="8D4840D6202CC371C32CE0576098";std::vector<std::complex<float>> block(blockSize,{.001f,0});
    for(auto slot:{0,2,7,9})block[slot]={.7f,.2f};
    for(unsigned bit=0;bit<112;++bit){const auto byte=std::stoul(hex.substr(bit/8*2,2),nullptr,16);block[16+bit*2+((byte>>(7-bit%8))&1?0:1)]={.7f,.2f};}
    Demodulator demod(rate);std::uint64_t frames=0;const auto start=std::chrono::steady_clock::now();
    for(std::uint64_t first=0;first<std::uint64_t(rate)*seconds;first+=blockSize)demod.feed(block,first,false,[&](const Frame& frame){if(frame.fields.callsign!="KLM1023")throw std::runtime_error("Unexpected stream frame");++frames;});
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"input_seconds="<<seconds<<" elapsed_seconds="<<elapsed<<" frames="<<frames<<" expected="<<rate*seconds/blockSize<<" realtime_factor="<<seconds/elapsed<<'\n';
    return frames==rate*seconds/blockSize?0:1;
}
