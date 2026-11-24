#include "plugins/adsb/live_input.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace signalscope;
int main() {
    adsb::LiveInput input;
    const IqStreamParameters outside{2000000,100000000,SampleFormat::cu8};
    Bytes bytes(2000,127);input.offer({bytes,0,outside,false});
    auto status=input.snapshot();
    if(status.offered_pairs!=1000 || status.rejected_pairs!=1000 || status.queued_bytes)throw std::runtime_error("Invalid RF input reached ADS-B queue");
    const IqStreamParameters valid{2000000,1090000000,SampleFormat::cu8};
    Bytes oversized(adsb::LiveInput::capacity_bytes+2,127);input.offer({oversized,0,valid,false});
    status=input.snapshot();if(status.queued_bytes || status.rejected_pairs!=status.offered_pairs)throw std::runtime_error("Oversized ADS-B input was not accounted");
    const std::string hex="8D4840D6202CC371C32CE0576098";
    for(const auto& codec:sample_codecs){adsb::LiveInput positive;std::vector<std::complex<float>> iq(240);
        for(auto slot:{0,2,7,9})iq[slot]={.7f,.2f};
        for(unsigned bit=0;bit<112;++bit){const auto byte=std::stoul(hex.substr(bit/8*2,2),nullptr,16);iq[16+bit*2+((byte>>(7-bit%8))&1?0:1)]={.7f,.2f};}
        Bytes encoded(iq.size()*codec.pair_bytes);for(std::size_t n=0;n<iq.size();++n)write_iq_pair(encoded.data()+n*codec.pair_bytes,codec.format,iq[n]);
        const IqStreamParameters stream{2000000,1090000000,codec.format};
        positive.offer({std::span(encoded).first(100*codec.pair_bytes),5000,stream,false});
        positive.offer({std::span(encoded).subspan(100*codec.pair_bytes),5100,stream,false});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do{status=positive.snapshot();if(status.processed_pairs==240)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
        if(status.processed_pairs!=240 || status.rejected_pairs || status.discontinuities || status.aircraft.size()!=1 || status.aircraft[0].callsign!="KLM1023")throw std::runtime_error("ADS-B live codec/worker path lost reference frame or counted startup as a gap");
    }
    std::cout<<"ADS-B queue rejection accounting passed\n";
}
