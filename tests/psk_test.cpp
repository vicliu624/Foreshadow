#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

int main() {
    using namespace signalscope;
    constexpr unsigned rate=1750000;
    constexpr double center=433920000,tau=2*std::numbers::pi;
    constexpr std::uint64_t first=9000000;
    std::mt19937 random(9831);
    std::normal_distribution<float> noise(0,.004f);
    std::vector<std::complex<float>> signal(rate/5);
    for(auto& z:signal)z={noise(random),noise(random)};
    for(const int order:{2,4}) {
        const auto offset=order==2?-120000:220000;
        const unsigned baud=order==2?2400:4800;
        const auto period=rate/baud;
        std::complex<float> previous{1,0};
        for(unsigned j=0;j<96;++j) {
            const auto symbol=std::polar(1.f,static_cast<float>(tau*(random()%order)/order));
            for(unsigned k=0;k<period;++k) {
                const auto rise=(1-std::cos(std::numbers::pi*std::min(k/(period*.2),1.)))/2;
                const auto index=j*period+k;
                signal[50000+index]+=(previous*float(1-rise)+symbol*float(rise))*
                    std::polar(.13f,static_cast<float>(tau*offset*index/rate));
            }
            previous=symbol;
        }
    }
    const auto encode=[&](SampleFormat format) {
        Bytes bytes;
        for(const auto value:signal)for(const auto component:{value.real(),value.imag()}) {
            if(format==SampleFormat::cu8)bytes.push_back(static_cast<std::uint8_t>(std::lround(component*128+127.5)));
            else {
                const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(component*32768)));
                bytes.push_back(static_cast<std::uint8_t>(word));
                bytes.push_back(static_cast<std::uint8_t>(word>>8));
            }
        }
        return bytes;
    };
    for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
        const auto bytes=encode(format);
        const auto pskOnly=detectPsk(bytes,format,rate,center,1000000,first);
        if(detectSnapshot(bytes,format,rate,center,1000000,first,{{true,true},true})!=pskOnly) {
            std::cerr<<"Enabling 4-FSK changed PSK evidence\n";
            return 5;
        }
        for(const auto selection:{PskSelection{},PskSelection{false,true},PskSelection{true,false}}) {
            const auto events=detectPsk(bytes,format,rate,center,1000000,first,selection);
            unsigned bpsk=0,qpsk=0;
            for(const auto& event:events) {
                const auto frequency=event.at("center_frequency_hz").get<double>();
                const auto sample=event.at("first_observed_sample").get<std::uint64_t>();
                if(sample<first+44000 || sample>first+56000)return 1;
                bpsk+=event.at("modulation")=="BPSK" && std::abs(frequency-center+120000)<1000;
                qpsk+=event.at("modulation")=="QPSK" && std::abs(frequency-center-220000)<1000;
            }
            if(bpsk!=unsigned(selection.bpsk) || qpsk!=unsigned(selection.qpsk) || events.size()!=bpsk+qpsk) {
                std::cerr<<sample_datatype(format)<<' '<<events.dump()<<'\n';
                return 2;
            }
        }
    }
    for(std::size_t i=0;i<signal.size();++i)
        signal[i]=(i>50000 && i<130000?.15f:0.f)*std::polar(1.f,static_cast<float>(tau*180000*i/rate));
    if(!detectPsk(encode(SampleFormat::ci16_le),SampleFormat::ci16_le,rate,center,1000000,0).empty())return 3;
    double phase=0;
    for(std::size_t i=0;i<signal.size();++i) {
        const auto tone=((i/(rate/2400))%2==0?-5000:5000);
        phase+=tau*(180000+tone)/rate;
        signal[i]=(i>50000 && i<130000?.15f:0.f)*std::polar(1.f,static_cast<float>(phase));
    }
    if(!detectPsk(encode(SampleFormat::ci16_le),SampleFormat::ci16_le,rate,center,1000000,0).empty())return 4;
}
