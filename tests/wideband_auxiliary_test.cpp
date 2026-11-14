#include "signalscope/backend/sessions/wideband_narrowband.hpp"
#include "signalscope/adapters/modulation/fm_audio.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi,center=100000000;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void encode(Bytes& bytes,std::complex<double> z) {
    for(auto part:{z.real(),z.imag()}){auto v=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::llround(std::clamp(part,-.999,.999)*32768)));bytes.push_back(v&255);bytes.push_back(v>>8);}
}
Bytes signal(std::uint32_t rate,double offset,const std::string& mode) {
    Bytes bytes;bytes.reserve(rate/5*4);double phase=0;
    std::mt19937 random(1234);std::normal_distribution<double> noise(0,.001);
    std::complex<double> previous{1,0},symbol{1,0};std::uint32_t oldSymbol=UINT32_MAX;
    for(std::uint32_t i=0;i<rate/5;++i) {
        const auto time=double(i)/rate;const auto index=std::uint32_t(time*2400);
        double amplitude=.24,frequency=offset;
        if(mode=="fsk")frequency+=(std::uint32_t(time*1000)%2?5000:-5000);
        if(mode=="4-fsk"){frequency+=((index*3)%4-1.5)*4500;if(time<.035 || time>.075)amplitude=0;}
        if(mode=="ook")amplitude*=std::uint32_t(time*400)%2?1:0;
        if(mode=="ask")amplitude*=std::uint32_t(time*400)%2?1:.3;
        if(mode=="fm")frequency+=15000*std::sin(tau*1000*time)+6500*std::sin(tau*19000*time);
        phase+=tau*frequency/rate;auto z=std::polar(amplitude,phase);
        if(mode=="bpsk" || mode=="qpsk") {
            if(index!=oldSymbol){previous=symbol;symbol=std::polar(1.,tau*(random()%(mode=="bpsk"?2:4))/(mode=="bpsk"?2:4));oldSymbol=index;}
            const auto rise=(1-std::cos(std::numbers::pi*std::min((time*2400-index)/.2,1.)))/2;
            z=std::polar(.24,tau*offset*time)*(previous*(1-rise)+symbol*rise);
            if(time<.035 || time>.075)z={};
        }
        encode(bytes,z+std::complex<double>(noise(random),noise(random)));
    }
    return bytes;
}
void narrow(std::uint32_t rate,double offset,const std::string& mode) {
    Config cfg;cfg.rate=rate;cfg.frequency=center;NarrowbandSelection selection{mode=="fsk",mode=="ook",mode=="ask",mode=="fm"};
    WidebandNarrowband detector(cfg,rate,SampleFormat::ci16_le,selection);NarrowbandEvents events;
    const auto bytes=signal(rate,offset,mode);constexpr std::uint64_t origin=712345;
    for(std::size_t at=0;at<bytes.size();at+=49148)detector.feed(std::span(bytes).subspan(at,std::min<std::size_t>(49148,bytes.size()-at)),origin+at/4,events);
    detector.finish(events);Json status;events.publish(status);
    const auto key=mode=="fm"?"recent_fm_events":mode=="fsk"?"recent_fsk_events":"recent_ask_events";
    bool found=false;
    for(const auto& e:status.at(key)) {
        if(std::abs(e.at("center_frequency_hz").get<double>()-center-offset)<2000)found=true;
        require(e.at("first_observed_sample").get<std::uint64_t>()>=origin && e.at("confirmed_at_sample").get<std::uint64_t>()<=origin+bytes.size()/4,"Narrowband event lost source sample indices");
    }
    if(!found)throw std::runtime_error(mode+" missing at "+std::to_string(rate)+" / "+std::to_string(offset)+": "+status.dump());
}
void fm(std::uint32_t rate,double offset) {
    const auto bytes=signal(rate,offset,"fm");std::vector<std::int16_t> a,b;
    const auto run=[&](std::size_t chunk,std::vector<std::int16_t>& pcm){FmDemodulator decoder({rate,SampleFormat::ci16_le,offset,0,75000});
        for(std::size_t at=0;at<bytes.size();at+=chunk)decoder.feed(std::span(bytes).subspan(at,std::min(chunk,bytes.size()-at)),at/4,[&](auto block){pcm.insert(pcm.end(),block.begin(),block.end());});};
    run(bytes.size(),a);run(19316,b);require(a==b && a.size()>9300,"Wideband FM audio is discontinuous or truncated");
    std::complex<double> tone{};for(std::size_t k=480;k<a.size();++k)tone+=double(a[k])/32768*std::polar(1.,-tau*1000*k/fm_audio_rate);
    const auto amplitude=2*std::abs(tone)/(a.size()-480);require(amplitude>.17 && amplitude<.23,"Wideband FM audio frequency or gain is wrong");
}
void multipleStations(std::uint32_t rate,bool stereo) {
    Config cfg;cfg.rate=rate;cfg.frequency=center;
    WidebandNarrowband detector(cfg,rate,SampleFormat::ci16_le,{false,false,false,true});
    const std::array<double,3> offsets{-450000,0,500000};
    std::array<double,3> phases{};
    NarrowbandEvents events;Bytes bytes;
    constexpr std::size_t chunk=16384;bytes.reserve(chunk*4);
    for(std::uint32_t i=0;i<rate/5;++i) {
        const auto t=double(i)/rate;std::complex<double> z{};
        for(std::size_t k=0;k<offsets.size();++k) {
            const auto deviation=15000*std::sin(tau*(900+100*k)*t)+(stereo?6500*std::sin(tau*19000*t):0);
            phases[k]+=tau*(offsets[k]+deviation)/rate;
            z+=std::polar(.2/(k+1),phases[k]);
        }
        encode(bytes,z);
        if(bytes.size()==chunk*4 || i+1==rate/5) {
            detector.feed(bytes,i+1-bytes.size()/4,events);bytes.clear();
        }
    }
    detector.finish(events);Json status;events.publish(status);
    const auto& found=status.at("recent_fm_events");
    if(!stereo){require(found.empty(),"FM without stereo evidence was mislabeled as confirmed stereo");return;}
    for(const auto offset:offsets) {
        bool matched=false;
        for(const auto& e:found)if(std::abs(e.at("center_frequency_hz").get<double>()-center-offset)<3000)matched=true;
        require(matched,"Multiple simultaneous FM stations in one analysis channel were lost");
    }
    for(const auto& e:found) {
        bool matched=false;
        for(const auto offset:offsets)if(std::abs(e.at("center_frequency_hz").get<double>()-center-offset)<3000)matched=true;
        require(matched,"FM detector reported a station outside the generated channels");
    }
}
void psk(std::uint32_t rate,double offset,const std::string& mode) {
    const auto bytes=signal(rate,offset,mode);const auto events=detectSnapshot(bytes,SampleFormat::ci16_le,rate,center,rate,9000000,{{mode=="bpsk",mode=="qpsk"},false});
    bool found=false;for(const auto& e:events)if(std::abs(e.at("center_frequency_hz").get<double>()-center-offset)<1000) {
        const auto t=(e.at("first_observed_sample").get<std::uint64_t>()-9000000)/double(rate);require(t>.03 && t<.04,"Wideband snapshot time mapping failed");found=true;}
    if(!found)throw std::runtime_error(mode+" snapshot missing: "+events.dump());
}
void snapshots(std::uint32_t rate) {
    const auto four=detectSnapshot(signal(rate,.4*rate,"4-fsk"),SampleFormat::ci16_le,rate,center,rate,0,{{false,false},true});
    require(four.size()==1 && four[0].at("modulation")=="4-FSK" && std::abs(four[0].at("center_frequency_hz").get<double>()-center-.4*rate)<1000,"Wideband 4-FSK channel was missed");
    Bytes css,ofdm;std::mt19937 random(9345);std::normal_distribution<double> noise(0,.001);
    const auto lag=rate/10000,prefix=lag/4,period=lag+prefix;
    std::vector<std::complex<double>> symbol(lag);
    for(std::uint32_t i=0;i<rate/5;++i) {
        const double t=double(i)/rate;
        auto c=std::complex<double>(noise(random),noise(random)),o=c;
        if(t>=.03 && t<.054) {const auto u=std::fmod(t-.03,.003);c+=std::polar(.2,tau*(-.4*rate*u+200000*(u*u/(2*.003)-u/2)));}
        if(i>=rate/25 && i<rate/25+100*period) {
            const auto k=(i-rate/25)%period;
            if(k==0)for(auto& z:symbol)z={noise(random)*120,noise(random)*120};
            o+=symbol[k<prefix?lag-prefix+k:k-prefix]*std::polar(1.,tau*.3*i);
        }
        encode(css,c);encode(ofdm,o);
    }
    const auto chirps=detectSnapshot(css,SampleFormat::ci16_le,rate,center,rate,543210,{{false,false},false,false,true});
    require(!chirps.empty() && chirps[0].at("modulation")=="CSS" && std::abs(chirps[0].at("center_frequency_hz").get<double>()-center+.4*rate)<5000,"Wideband CSS edge train was missed");
    const auto prefixes=detectSnapshot(ofdm,SampleFormat::ci16_le,rate,center,rate,543210,{{false,false},false,true,false});
    require(prefixes.size()==1 && std::abs(prefixes[0].at("useful_symbol_seconds").get<double>()-double(lag)/rate)<1./rate,"Wideband cyclic-prefix timing was lost");
}
}
int main(){try {
    for(auto rate:{2560000u,10000000u}) {
        for(auto offset:{-.4*rate,.4*rate})for(const auto* mode:{"fsk","ook","ask","fm"})narrow(rate,offset,mode);
        fm(rate,.4*rate);psk(rate,-.4*rate,"bpsk");psk(rate,.4*rate,"qpsk");
        snapshots(rate);
        multipleStations(rate,true);multipleStations(rate,false);
    }
    narrow(10000000,4200195.3125,"ook");narrow(10000000,4200195.3125,"ask");
    std::cout<<"Wideband FSK, OOK, ASK, FM pilot/audio, PSK, 4-FSK, CSS and OFDM coverage passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
