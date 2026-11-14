#include "signalscope/adapters/modulation/wideband_lora.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/backend/dsp/parallel_channels.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>
using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Bytes waveform(std::uint32_t rate,const std::vector<double>& offsets,bool inverted=false,int sf=7) {
    const auto symbols=std::ldexp(1.,sf);
    const double period=double(rate)*symbols/125000;
    const auto count=std::max<std::uint32_t>(rate/40,static_cast<std::uint32_t>(std::ceil(rate*.001+16*period)));Bytes bytes;bytes.reserve(count*4);
    for(std::uint32_t i=0;i<count;++i) {
        std::complex<double> z{};const double position=(double(i)-rate*.001)/period;
        if(position>=0 && position<15.25)for(const auto offset:offsets) {
            double shift=0,time=position;int direction=1;
            if(position>=8 && position<9)shift=24;
            else if(position>=9 && position<10)shift=32;
            else if(position>=10 && position<12.25)direction=-1;
            else if(position>=12.25){shift=56;time-=.25;}
            time+=shift/symbols;const auto fraction=time-std::floor(time);
            const auto phase=symbols*(fraction*fraction-fraction)/2;
            z+=std::polar(.16,tau*((inverted?-1:1)*direction*phase+offset*i/rate));
        }
        for(const auto value:{z.real(),z.imag()}) {
            const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::llround(value*32768)));
            bytes.push_back(word&255);bytes.push_back(word>>8);
        }
    }
    return bytes;
}
void check(std::uint32_t rate,std::vector<double> offsets,bool all=false,bool inverted=false,int sf=7) {
    LoRaConfig c;c.rate=rate;c.bandwidth=125000;c.sf_min=c.sf_max=sf;c.search_width=rate;c.workers=2;
    if(all)c.bandwidth_candidates={125000,250000,500000};
    WidebandLoRa detector(c,SampleFormat::ci16_le);const auto bytes=waveform(rate,offsets,inverted,sf);
    for(std::size_t at=0;at<bytes.size();) {
        const auto count=std::min<std::size_t>(bytes.size()-at,12344);
        detector.feed(std::span(bytes).subspan(at,count),at/4);at+=count;
    }
    detector.finish();const auto result=detector.results();const auto events=result.at("events");
    for(const auto offset:offsets) {
        bool found=false;unsigned matches=0;
        for(const auto& event:events) {
            if(event.value("evidence_level",std::string())!="lora_structure_match" || event.value("iq_inverted",!inverted)!=inverted)continue;
            if(std::abs(event.at("center_frequency_hz").get<double>()-c.receiver_frequency-offset)>800)continue;
            ++matches;found=true;
            const auto expected=rate*.001+10*double(rate)*std::ldexp(1.,sf)/125000;
            require(std::abs(event.at("sfd_start_sample").get<double>()-expected)<rate*.00003,
                "Wideband event timing was not mapped back to original source samples");
        }
        if(!found) {
            std::cerr<<"Missing "<<offset<<" Hz at "<<rate<<": "<<events.dump()<<'\n';
            for(const auto& work:result.at("search_work"))if(work.at("analysis_channel_offset_hz").get<double>()< -4000000 && work.at("bandwidth_hz")==125000 && work.at("inverted")==false)std::cerr<<work.dump()<<'\n';
        }
        require(found,"LoRa outside the source center was not detected");
        require(matches==1,"Overlapping analysis channels duplicated a LoRa packet");
    }
    require(result.at("analysis").at("source_pairs")==bytes.size()/4,"Channelized processing lost source sample accounting");
    detector.reset();require(detector.status().at("source_pairs")==0,"Wideband gap reset retained old timeline state");
}
void sharedSearchRegression() {
    LoRaConfig c;c.rate=10000000;c.search_width=c.rate;c.bandwidth_candidates=defaultLiveLoRaBandwidths();
    c.sf_min=7;c.sf_max=9;
    const auto bytes=waveform(c.rate,{-4200000,4200000},false,9);
    const auto run=[&](unsigned workers){
        c.workers=workers;WidebandLoRa detector(c,SampleFormat::ci16_le);
        for(std::size_t at=0;at<bytes.size();) {
            const auto count=std::min<std::size_t>(bytes.size()-at,49152*4);
            detector.feed(std::span(bytes).subspan(at,count),at/4);at+=count;
        }
        detector.finish();return detector.results();
    };
    const auto serial=run(1),parallel=run(12);
    unsigned structures=0;
    for(const auto& event:parallel.at("events"))if(event.value("evidence_level",std::string())=="lora_structure_match")++structures;
    require(structures==2,"Shared search scheduling lost or duplicated simultaneous signals");
    require(serial.at("events")==parallel.at("events"),"Worker count changed LoRa event evidence or source timing");
    require(serial.at("search_work")==parallel.at("search_work"),"Shared workers skipped or repeated search work");
    require(parallel.at("analysis").at("workers")==12,"Independent search models did not use the requested worker budget");

    LoRaConfig local;local.sf_min=local.sf_max=7;
    LoRaDetector left(local,SampleFormat::ci16_le,true),right(local,SampleFormat::ci16_le,true);
    ParallelChannels pool(3);std::vector<std::complex<float>> silence(8192);
    std::array<LoRaFloatBlock,2> blocks{{{&left,silence,0},{&right,silence,1}}};
    bool rejected=false;
    try{feedLoRaChannels(blocks,pool);}catch(const std::runtime_error&){rejected=true;}
    require(rejected && left.results().at("source_pairs")==0 && right.results().at("source_pairs")==0,
        "Invalid shared batch partially advanced another channel");
    blocks[1].first_sample=0;feedLoRaChannels(blocks,pool);
    left.feedFloat(silence,silence.size());
    require(left.results().at("source_pairs")==2*silence.size() && right.results().at("source_pairs")==silence.size(),
        "Shared and independent feeding did not preserve separate channel clocks");
    require(left.take_events().empty() && right.take_events().empty(),"Silent channels acquired invented evidence");
}
}
int main() {
    try {
        std::atomic<bool> planningFailed=false;std::vector<std::jthread> planners;
        for(unsigned worker=0;worker<4;++worker)planners.emplace_back([&]{try {
            for(unsigned repeat=0;repeat<6;++repeat){
                Channelizer converter(10000000,2000000,SampleFormat::ci16_le,{{0,1000000}});
                LoRaConfig local;local.sf_min=local.sf_max=7;LoRaDetector detector(local,SampleFormat::ci16_le,true);
            }
        }catch(...){planningFailed=true;}});
        planners.clear();require(!planningFailed,"Concurrent FFT plan construction failed");
        sharedSearchRegression();
        check(10000000,{-4200000,0,4200000});
        check(10000000,{-4900000,4900000},true);check(2560000,{-1190000,1190000},true);
        check(10000000,{-4900000,4900000},true,true);check(2560000,{-1190000,1190000},true,true);
        check(10000000,{-4900000,4900000},true,false,9);
        for(const auto rate:{2048000u,2400000u,2560000u,3200000u})check(rate,{-.35*rate,.35*rate});
        std::cout<<"Wideband LoRa edge coverage, fractional acquisition rates, original timing and overlap deduplication passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
