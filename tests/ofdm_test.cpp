#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

int main() {
    using namespace signalscope;
    using Complex=std::complex<float>;
    constexpr std::uint32_t rate=1750000;
    constexpr std::uint64_t first=9200000;
    constexpr double center=433920000,tau=2*std::numbers::pi;
    struct Burst {std::size_t start;int lag,prefix,offset;};
    const std::array bursts{Burst{35000,384,48,-187000},Burst{157500,256,64,220000},
                            Burst{262500,256,0,-120000}};
    std::mt19937 random(261106);
    std::normal_distribution<float> noise(0,.004f);
    std::vector<Complex> samples(rate/4);
    for(auto& sample:samples)sample={noise(random),noise(random)};
    for(const auto burst:bursts) {
        std::vector<Complex> waveform;
        for(int symbol=0;symbol<100;++symbol) {
            std::vector<Complex> time(burst.lag);
            for(int bin=-burst.lag/4;bin<burst.lag/4;++bin) {
                if(!bin)continue;
                const auto step=std::polar(1.f,static_cast<float>(tau*bin/burst.lag));
                Complex value(random()&1?1.f:-1.f,random()&1?1.f:-1.f);
                for(auto& sample:time){sample+=value;value*=step;}
            }
            waveform.insert(waveform.end(),time.end()-burst.prefix,time.end());
            waveform.insert(waveform.end(),time.begin(),time.end());
        }
        const auto scale=.12f/std::sqrt(float(burst.lag-2));
        for(std::size_t k=0;k<waveform.size();++k) {
            const auto delayed=k>=3?waveform[k-3]*.22f:Complex{};
            samples[burst.start+k]+=(waveform[k]+delayed)*scale*
                std::polar(1.f,static_cast<float>(tau*burst.offset*k/rate));
        }
    }
    for(std::size_t k=350000;k<405000;++k)
        samples[k]+=std::polar(.18f,static_cast<float>(tau*110000*k/rate));
    for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
        Bytes bytes;
        for(const auto value:samples)for(const auto component:{value.real(),value.imag()}) {
            if(format==SampleFormat::cu8)bytes.push_back(static_cast<std::uint8_t>(std::lround(component*128+127.5)));
            else {
                const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(component*32768)));
                bytes.push_back(static_cast<std::uint8_t>(word));
                bytes.push_back(static_cast<std::uint8_t>(word>>8));
            }
        }
        const auto events=detectSnapshot(bytes,format,rate,center,1000000,first,{{false,false},false,true});
        if(events.size()!=2){std::cerr<<events.dump()<<'\n';return 1;}
        for(std::size_t i=0;i<events.size();++i) {
            const auto& event=events[i];
            if(event.at("modulation")!="OFDM" ||
               std::llround(event.at("useful_symbol_seconds").get<double>()*rate)!=bursts[i].lag ||
               std::llround(event.at("prefix_seconds").get<double>()*rate)!=bursts[i].prefix ||
               std::abs(event.at("first_observed_sample").get<double>()-first-bursts[i].start)>3500) {
                std::cerr<<event.dump()<<'\n';return 2;
            }
        }
        if(!detectSnapshot(bytes,format,rate,center,1000000,first,{{false,false},false,false}).empty())return 3;
    }
}
