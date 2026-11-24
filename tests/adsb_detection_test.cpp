#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <iostream>
using namespace signalscope;
int main() {
    constexpr unsigned rate=2000000;const std::string hex="8D4840D6202CC371C32CE0576098";
    std::vector<std::complex<float>> iq(240);for(auto slot:{0,2,7,9})iq[slot]={.7f,.2f};
    for(unsigned bit=0;bit<112;++bit){const auto byte=std::stoul(hex.substr(bit/8*2,2),nullptr,16);iq[16+bit*2+((byte>>(7-bit%8))&1?0:1)]={.7f,.2f};}
    SnapshotSelection selection{{false,false},false,false,false,{}};selection.extensions["adsb"]=true;
    for(const auto& codec:sample_codecs){Bytes bytes(iq.size()*codec.pair_bytes);for(std::size_t n=0;n<iq.size();++n)write_iq_pair(bytes.data()+n*codec.pair_bytes,codec.format,iq[n]);
        const auto events=detectSnapshot(bytes,codec.format,rate,1090000000.,rate,12345,selection);
        if(events.size()!=1 || events[0].at("modulation_id")!="adsb" || events[0].at("raw_message")!=hex || events[0].at("first_observed_sample")!=12345 || !events[0].contains("display_label") || events[0].at("features").size()<3)throw std::runtime_error("ADS-B generic event contract failed");
        if(!detectSnapshot(bytes,codec.format,rate,100000000.,rate,0,selection).empty())throw std::runtime_error("ADS-B accepted unrelated RF coverage");
    }
    std::cout<<"ADS-B generic detector and codec contract passed\n";
}
