#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

int main() {
    using namespace signalscope;
    constexpr unsigned rate=1750000;
    constexpr std::uint64_t origin=8400000;
    constexpr double center=478500000,tau=2*std::numbers::pi;
    using Complex=std::complex<float>;
    struct Sweep {double start,period,offset,span;};
    const std::array cases{Sweep{.03,.0017,-200000,187000},Sweep{.12,.0023,170000,-213000},
                           Sweep{.165,.00039,-170000,360000},Sweep{.21,.0065,210000,-201000}};
    std::mt19937 random(261901);
    std::normal_distribution<float> noise(0,.004f);
    std::vector<Complex> signal(500000);
    for(std::size_t k=0;k<signal.size();++k)
        signal[k]=Complex(noise(random),noise(random))+std::polar(.05f,static_cast<float>(tau*340000*k/rate));
    const auto insert=[&](const Sweep& sweep,unsigned repeats) {
        const auto first=std::size_t(std::llround(sweep.start*rate));
        const auto length=std::size_t(std::llround(sweep.period*rate));
        for(unsigned chirp=0;chirp<repeats;++chirp)for(std::size_t k=0;k<length;++k) {
            const auto t=double(k)/rate;
            const auto phase=tau*(sweep.offset*t+sweep.span*(t*t/(2*sweep.period)-t/2));
            signal[first+chirp*length+k]+=std::polar(.15f,static_cast<float>(phase));
        }
    };
    for(const auto sweep:cases)insert(sweep,8);
    insert({180000./rate,.002,90000,150000},1);
    double phase=0;
    for(unsigned k=0;k<50*729;++k) {
        phase+=tau*(-320000+(k/729%2?4500:-4500))/rate;
        signal[90000+k]+=std::polar(.13f,static_cast<float>(phase));
    }
    for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
        Bytes bytes;
        for(const auto value:signal)for(const auto component:{value.real(),value.imag()}) {
            if(format==SampleFormat::cu8)bytes.push_back(static_cast<std::uint8_t>(std::lround(127.5+component*128)));
            else {
                const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(component*32768)));
                bytes.push_back(static_cast<std::uint8_t>(word));
                bytes.push_back(static_cast<std::uint8_t>(word>>8));
            }
        }
        const auto result=detectSnapshot(bytes,format,rate,center,1000000,origin,{{false,false},false,false,true});
        if(result.size()!=cases.size()){std::cerr<<result.dump()<<'\n';return 1;}
        for(std::size_t i=0;i<cases.size();++i) {
            const auto& event=result[i];
            const auto& expected=cases[i];
            if(event.at("modulation")!="CSS" || event.at("repeated_chirps")!=8 ||
               std::abs(event.at("center_frequency_hz").get<double>()-center-expected.offset)>3000 ||
               std::abs(event.at("repeat_interval_seconds").get<double>()/expected.period-1)>.05 ||
               std::abs(event.at("sweep_rate_hz_per_second").get<double>()/(expected.span/expected.period)-1)>.08 ||
               std::abs((event.at("first_observed_sample").get<double>()-origin)/rate-expected.start)>.0002) {
                std::cerr<<event.dump()<<'\n';
                return 2;
            }
        }
        if(!detectSnapshot(bytes,format,rate,center,1000000,origin,{{false,false},false,false,false}).empty())return 3;
    }
}
