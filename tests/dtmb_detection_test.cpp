#include "dtmb_fixture.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include "signalscope/adapters/modulation/descriptor.hpp"
#include "signalscope/backend/sessions/snapshot_analysis.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
using namespace signalscope;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Bytes encode(std::span<const std::complex<float>> samples,SampleFormat format) {
    Bytes output(samples.size()*bytes_per_pair(format));auto* out=output.data();
    for(const auto sample:samples)for(const auto value:{sample.real(),sample.imag()}) {
        if(format==SampleFormat::cu8)*out++=static_cast<std::uint8_t>(std::clamp(std::round(value*128+127.5f),0.f,255.f));
        else if(format==SampleFormat::ci16_le) {
            const auto bits=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(std::round(value*32768),-32768.f,32767.f)));
            *out++=bits&255;*out++=bits>>8;
        }else {const auto bits=std::bit_cast<std::uint32_t>(value);for(int byte=0;byte<4;++byte)*out++=(bits>>(8*byte))&255;}
    }
    return output;
}
SnapshotSelection selection(){SnapshotSelection result{{false,false},false,false,false};result.extensions["dtmb"]=true;return result;}
Json detect(const std::vector<std::complex<float>>& samples,unsigned rate,SampleFormat format=SampleFormat::ci16_le,double width=10e6) {
    return detectSnapshot(encode(samples,format),format,rate,610e6,std::min<double>(width,rate),1ULL<<54,selection());
}
void positive(dtmb_fixture::Options options,SampleFormat format=SampleFormat::ci16_le) {
    const auto samples=dtmb_fixture::make(options);const auto started=std::chrono::steady_clock::now();
    const auto events=detect(samples,options.source_rate,format);std::cout<<"PN"<<options.header<<" C"<<options.body<<" Fs="<<options.source_rate<<" CFO="<<options.offset<<" SNR="<<options.snr<<": "<<events.dump()<<'\n';
    require(events.size()==1,"Independent DTMB framing fixture was not uniquely identified");const auto& event=events[0];
    require(event.at("header_symbols")==options.header && event.at("checked_frames").get<unsigned>()>=4,"Wrong DTMB framing mode");
    require(std::abs(event.at("estimated_frequency_offset_hz").get<double>()-options.offset)<45000,"DTMB center frequency estimate is incorrect");
    const auto relative=event.at("first_observed_sample").get<std::uint64_t>()-(1ULL<<54);
    const auto expected=(options.header+3780-options.timing)*options.source_rate/dtmb_fixture::rate;
    require(std::abs(double(relative)-expected)<30,"DTMB sample origin or fractional rate mapping changed");
    require(event.at("payload_decoded")==false && event.contains("features") && event.at("display_label").contains("en") && event.at("display_label").contains("zh-CN"),"DTMB evidence or presentation contract was lost");
    std::cout<<"analysis_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="--resource-only") {
            for(const auto& descriptor:modulationDescriptors())if(descriptor.id=="dtmb") {
                DetectorContext context;context.receiver.rate=10000000;context.maximum_span_hz=10000000;
                const auto info=descriptor.describe(context);
                require(info.at("label").at("fr")=="Télévision terrestre DTMB","Resource-only language did not reach detector descriptor");
                require(!info.at("parsing").at("message").contains("fr") && info.at("parsing").at("message").contains("en"),"Missing translation lost English fallback");
                require(info.at("covered")==true,"Compatible DTMB configuration was rejected");context.receiver.if_bandwidth_hz=5000000;
                require(descriptor.describe(context).at("covered")==false,"Narrow receiver filter incorrectly advertised full DTMB coverage");
                return 0;
            }
            throw std::runtime_error("DTMB descriptor was not composed");
        }
        if(argc>2 && std::string(argv[1])=="--generate") {
            Config config;config.rate=10000000;config.frequency=610000000;
            Recorder recorder(path_from_utf8(argv[2]),config,"independent-dtmb-framing",SampleFormat::ci16_le);
            recorder.append(encode(dtmb_fixture::make(),SampleFormat::ci16_le));recorder.finish("complete");return 0;
        }
        for(const auto header:{420u,595u,945u})for(const auto body:{1u,3780u})positive({header,body});
        auto additional=dtmb_fixture::Options{};additional.header=945;additional.source_rate=9600000;additional.offset=-650000;additional.snr=6;additional.echo=.35;
        positive(additional,SampleFormat::cf32_le);
        additional.header=595;additional.source_rate=8000000;additional.offset=0;additional.echo=.15;positive(additional,SampleFormat::cu8);
        additional.header=420;additional.source_rate=10666000;additional.offset=700000;additional.echo=0;positive(additional);
        {
            auto left=additional;left.header=420;left.source_rate=20000000;left.offset=-5000000;left.snr=15;
            auto right=left;right.offset=5000000;right.timing=767.23;
            auto samples=dtmb_fixture::make(left);const auto second=dtmb_fixture::make(right);
            for(std::size_t i=0;i<samples.size();++i)samples[i]+=second[i];
            const auto events=detect(samples,left.source_rate,SampleFormat::cf32_le,20000000);
            std::cout<<"Two arbitrary-center channels: "<<events.dump()<<'\n';
            require(events.size()==2,"Wideband DTMB bank did not identify both complete channels");
        }
        // The same RF signal must disappear when its center is outside the
        // search interval; increasing source rate must not change this rule.
        require(detect(dtmb_fixture::make(additional),additional.source_rate,SampleFormat::ci16_le,100000).empty(),"DTMB ignored Search width");
        for(int kind=0;kind<4;++kind) {
            auto options=dtmb_fixture::Options{};options.one_header=kind==0;options.period_error=kind==1?37:0;options.wrong_pn=kind==2;options.continuous_pn=kind==3;
            require(detect(dtmb_fixture::make(options),options.source_rate).empty(),"Invalid PN/frame timing was identified as DTMB");
        }
        std::mt19937 random(17);std::normal_distribution<float> normal(0,.1f);std::vector<std::complex<float>> negative(160000);
        for(int kind=0;kind<4;++kind) {
            for(std::size_t i=0;i<negative.size();++i) {
                const auto time=i/1e7;
                negative[i]=kind==0?std::complex<float>(normal(random),normal(random)):
                    kind==1?std::polar(.1f,static_cast<float>(std::remainder(dtmb_fixture::tau*123456*time,dtmb_fixture::tau))):
                    kind==2?std::polar(.1f,static_cast<float>(30*std::sin(dtmb_fixture::tau*1000*time)+3*std::sin(dtmb_fixture::tau*7000*time))):std::complex<float>{};
            }
            if(kind==3) {std::vector<std::complex<float>> symbol(1024);for(auto& value:symbol)value={normal(random),normal(random)};
                for(std::size_t i=0;i<negative.size();++i)negative[i]=symbol[(i%1152+896)%1024];}
            require(detect(negative,10000000).empty(),"Noise, tone, FM or CP-OFDM was misidentified as DTMB");
        }
        require(detect(dtmb_fixture::make(),2000000).empty(),"Insufficient source bandwidth was accepted");
        {
            const auto bytes=encode(dtmb_fixture::make(),SampleFormat::ci16_le);
            SnapshotAnalysis live;live.reset(selection());auto feed=live.makeFeed(10000000);
            const IqStreamParameters stream{10000000,610000000,SampleFormat::ci16_le};const auto origin=1ULL<<54;
            for(std::size_t i=0;i<bytes.size();i+=65536) {
                const auto part=std::span(bytes).subspan(i,std::min<std::size_t>(65536,bytes.size()-i));
                feed({part,origin+i/bytes_per_pair(stream.format),stream,i==0});live.poll();
            }
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(live.status().at("busy").get<bool>() && std::chrono::steady_clock::now()<deadline){live.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            require(live.status().at("completed")==1 && live.status().at("observation_policies")==1 && live.events().size()==1,"Periodic DTMB did not reach the live snapshot pipeline");
            require(live.events()[0].at("first_observed_sample").get<std::uint64_t>()>origin,"Live DTMB lost its source sample clock");
        }
        std::cout<<"DTMB independent positive/negative fixtures passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
