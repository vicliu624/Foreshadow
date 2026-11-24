#include "plugins/adsb/core/codec.hpp"
#include "plugins/adsb/core/demod.hpp"
#include "plugins/adsb/core/tracker.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <random>
using namespace signalscope::adsb;
namespace {
Message message(const std::string& hex) {
    Message out{};for(unsigned n=0;n<14;++n)out[n]=static_cast<std::uint8_t>(std::stoul(hex.substr(n*2,2),nullptr,16));return out;
}
void require(bool value){if(!value)throw std::runtime_error("ADS-B reference mismatch");}
Message df18(unsigned control) {
    auto result=message("8D4840D6202CC371C32CE0576098");result[0]=std::uint8_t((18u<<3)|control);
    std::array<unsigned,112> dividend{};for(unsigned n=0;n<88;++n)dividend[n]=(result[n/8]>>(7-n%8))&1;
    for(unsigned n=0;n<88;++n)if(dividend[n])for(unsigned bit=0;bit<=24;++bit)dividend[n+bit]^=(0x1fff409u>>(24-bit))&1;
    result[11]=result[12]=result[13]=0;for(unsigned n=88;n<112;++n)result[n/8]|=std::uint8_t(dividend[n]<<(7-n%8));return result;
}
}
int main() {
    const auto identity=decode(message("8D4840D6202CC371C32CE0576098"));
    require(identity && identity->address==0x4840d6 && identity->callsign=="KLM1023");
    const auto position=decode(message("8D40621D58C382D690C8AC2863A7"));
    require(position && position->altitude_ft==38000 && position->cpr && !position->odd);
    const auto odd=decode(message("8D40621D58C386435CC412692AD6"));require(bool(odd));
    const auto location=airborne_position(*position,1,*odd,0);
    require(location && std::abs(location->latitude-52.257202)<.00001 && std::abs(location->longitude-3.919373)<.00001);
    require(!airborne_position(*position,20,*odd,0));
    Tracker tracker;tracker.observe(*odd,0);tracker.observe(*position,1);
    require(tracker.snapshot(1).size()==1 && tracker.snapshot(1)[0].position.has_value());
    require(!tracker.snapshot(17)[0].position && tracker.snapshot(122).empty());
    tracker.clear();tracker.observe(*odd,0);tracker.clear();tracker.observe(*position,1);require(!tracker.snapshot(1)[0].position);
    auto damaged=message("8D40621D58C382D690C8AC2863A7");damaged[6]^=1;require(!decode(damaged));
    require(!decode(Message{}));
    for(unsigned control=0;control<8;++control){const auto parsed=decode(df18(control));require(bool(parsed));
        const bool supported=control!=3 && control!=4 && control!=7;require(parsed->aircraft_fields==supported);
        Tracker tracks;tracks.observe(*parsed,0);require(tracks.snapshot(0).size()==unsigned(supported));
        if(!supported)require(parsed->callsign.empty() && !parsed->cpr && !parsed->altitude_ft);
    }
    for(const unsigned rate:{2000000u,2400000u,4000000u}) {
        const auto source=message("8D4840D6202CC371C32CE0576098");
        std::vector<std::complex<float>> iq((std::uint64_t(rate)*120+999999)/1000000);
        const auto pulse=[&](unsigned slot){const auto a=std::size_t(std::llround(double(slot)*rate/2000000)),b=std::min(iq.size(),std::size_t(std::llround(double(slot+1)*rate/2000000)));for(auto n=a;n<b;++n)iq[n]={.7f,.2f};};
        for(auto slot:{0u,2u,7u,9u})pulse(slot);
        for(unsigned bit=0;bit<112;++bit)pulse(16+2*bit+((source[bit/8]>>(7-bit%8))&1?0:1));
        for(const double phase:{.1,.3,.5,.7,.9}){
            std::vector<std::complex<float>> shifted(iq.size()+4);
            for(std::size_t n=0;n<shifted.size();++n){const auto position=(n+phase)*2000000/rate;if(position>=240)continue;const auto slot=unsigned(position);
                const bool high=slot<16?(slot==0 || slot==2 || slot==7 || slot==9):((slot-16)%2==(((source[(slot-16)/16]>>(7-((slot-16)/2)%8))&1)?0u:1u));
                if(high)shifted[n]={.7f,.2f};}
            Demodulator demod(rate);unsigned count=0;demod.feed(shifted,0,false,[&](const Frame& frame){require(frame.bytes==source);++count;});require(count==1);
        }
        for(const float amplitude:{.05f,.5f,1.f}){
            std::mt19937 generator(1090);std::normal_distribution<float> noise(0,.0002f);
            auto noisy=iq;for(auto& value:noisy)value=value*amplitude+std::complex<float>(noise(generator),noise(generator));
            Demodulator demod(rate);unsigned count=0;demod.feed(noisy,0,false,[&](const Frame& frame){require(frame.bytes==source);++count;});require(count==1);
        }
        {
            std::mt19937 generator(17);std::normal_distribution<float> noise(0,.2f);std::vector<std::complex<float>> background(100000);
            for(auto& value:background)value={noise(generator),noise(generator)};
            Demodulator demod(rate);unsigned count=0;demod.feed(background,0,false,[&](const Frame&){++count;});require(count==0);
        }
        {
            Demodulator demod(rate);unsigned count=0;
            std::vector<std::complex<float>> background(iq.size()*2,{.001f,0});
            const auto sink=[&](const Frame& frame){require(frame.bytes==source && frame.first_sample==background.size());++count;};
            demod.feed(background,0,false,sink);require(count==0);
            demod.feed(iq,background.size(),false,sink);require(count==1);
        }
        for(std::size_t split=1;split<iq.size();++split){Demodulator demod(rate);unsigned count=0;
            const auto sink=[&](const Frame& frame){require(frame.bytes==source && frame.first_sample==100);++count;};
            demod.feed(std::span(iq).first(split),100,false,sink);demod.feed(std::span(iq).subspan(split),100+split,false,sink);require(count==1);
            demod.reset();count=0;demod.feed(std::span(iq).first(split),100,false,sink);demod.feed(std::span(iq).subspan(split),100+split,true,sink);require(count==0);
        }
    }
    std::cout<<"ADS-B reference frames and corrupt-frame rejection passed\n";
}
