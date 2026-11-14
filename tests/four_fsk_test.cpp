#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

int main() {
    using namespace signalscope;
    constexpr unsigned rate=1750000;
    constexpr double center=433920000,carrier=180000,tau=2*std::numbers::pi;
    constexpr std::uint64_t first=9000000;
    constexpr std::array tones{-6750.,-2250.,2250.,6750.};
    for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
        for(const int count:{2,4}) {
            std::mt19937 random(9132);
            std::normal_distribution<float> noise(0,.004f);
            Bytes bytes;
            double phase=0;
            for(unsigned i=0;i<rate/5;++i) {
                const auto symbol=((i/(rate/2400))*3)%count;
                phase+=tau*(carrier+tones[symbol])/rate;
                const auto sample=std::polar(i>=50000 && i<130000?.15f:0.f,static_cast<float>(phase));
                for(const auto component:{sample.real()+noise(random),sample.imag()+noise(random)}) {
                    if(format==SampleFormat::cu8)
                        bytes.push_back(static_cast<std::uint8_t>(std::lround(127.5+component*128)));
                    else {
                        const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(component*32768)));
                        bytes.push_back(static_cast<std::uint8_t>(word));
                        bytes.push_back(static_cast<std::uint8_t>(word>>8));
                    }
                }
            }
            const auto events=detectSnapshot(bytes,format,rate,center,1000000,first,{{false,false},true});
            if(count==2) {
                if(!events.empty())return 1;
                continue;
            }
            if(events.size()!=1 || events[0].at("modulation")!="4-FSK") {
                std::cerr<<events.dump()<<'\n';
                return 2;
            }
            const auto& event=events[0];
            if(std::abs(event.at("center_frequency_hz").get<double>()-center-carrier)>250)return 3;
            const auto observed=event.at("first_observed_sample").get<std::uint64_t>();
            if(observed<first+44000 || observed>first+56000)return 4;
            for(std::size_t i=0;i<tones.size();++i)
                if(std::abs(event.at("tone_offsets_hz")[i].get<double>()-tones[i])>250)return 5;
            if(!detectSnapshot(bytes,format,rate,center,1000000,first,{{true,true},false}).empty())return 6;
            const auto combined=detectSnapshot(bytes,format,rate,center,1000000,first,{{true,true},true});
            if(combined!=events) {
                std::cerr<<"4-FSK fallback changed with PSK enabled: "<<combined.dump()<<'\n';
                return 7;
            }
        }
    }
}
