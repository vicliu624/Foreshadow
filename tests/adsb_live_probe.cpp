#include "plugins/adsb/live_input.hpp"
#include <iostream>
#include <random>
#include <thread>
int main() {
    using namespace signalscope;constexpr unsigned rate=2000000,blockSize=20000,blocks=6000;
    std::mt19937 generator(1090);std::normal_distribution<float> noise(0,.004f);
    std::vector<std::complex<float>> iq(blockSize);
    for(auto& value:iq)value={noise(generator),noise(generator)};
    const std::string hex="8D4840D6202CC371C32CE0576098";
    for(auto slot:{0,2,7,9})iq[slot]={.7f,.2f};for(unsigned bit=0;bit<112;++bit){const auto byte=std::stoul(hex.substr(bit/8*2,2),nullptr,16);iq[16+2*bit+((byte>>(7-bit%8))&1?0:1)]={.7f,.2f};}
    const auto format=SampleFormat::ci16_le;const auto stride=bytes_per_pair(format);Bytes bytes(blockSize*stride);for(unsigned n=0;n<blockSize;++n)write_iq_pair(bytes.data()+n*stride,format,iq[n]);
    adsb::LiveInput input;const IqStreamParameters stream{rate,1090000000.,format};const auto start=std::chrono::steady_clock::now();
    for(unsigned block=0;block<blocks;++block){input.offer({bytes,std::uint64_t(block)*blockSize,stream,false});std::this_thread::sleep_until(start+std::chrono::milliseconds(10*(block+1)));}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);adsb::LiveSnapshot result;
    do{result=input.snapshot();if(result.processed_pairs+result.rejected_pairs==std::uint64_t(blocks)*blockSize)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
    const auto frames=result.aircraft.empty()?0:result.aircraft[0].messages;
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"input_seconds=60 elapsed_seconds="<<elapsed<<" offered="<<result.offered_pairs<<" processed="<<result.processed_pairs<<" rejected="<<result.rejected_pairs<<" gaps="<<result.discontinuities<<" frames="<<frames<<" high_water_bytes="<<result.high_water_bytes<<'\n';
    return result.rejected_pairs==0 && result.processed_pairs==std::uint64_t(blocks)*blockSize && frames==blocks && result.discontinuities==0?0:1;
}
