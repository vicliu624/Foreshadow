#include "signalscope/backend/dsp/recovery.hpp"
#include <iostream>
using namespace signalscope;
namespace {
void require(bool value){if(!value)throw std::runtime_error("ADS-B recovery contract failed");}
std::shared_ptr<SignalWindow> fixture(bool gap) {
    auto source=std::make_shared<SignalWindow>();source->stream={2000000,1090000000,SampleFormat::cf32_le};source->source_id="adsb-reference";source->recording_origin=1ULL<<50;
    const std::string hex="8D4840D6202CC371C32CE0576098";std::vector<std::complex<float>> iq(240);
    for(auto n:{0,2,7,9})iq[n]={.7f,.2f};
    for(unsigned bit=0;bit<112;++bit){const auto byte=std::stoul(hex.substr(bit/8*2,2),nullptr,16);iq[16+bit*2+((byte>>(7-bit%8))&1?0:1)]={.7f,.2f};}
    source->segments.push_back({source->recording_origin,{iq.begin(),iq.begin()+100},false});
    source->segments.push_back({source->recording_origin+100,{iq.begin()+100,iq.end()},gap});return source;
}
RecoveryResult run(bool gap,RecoveryBudget budget={}){auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(fixture(gap)));return recover_signal(analysis,{analysis->first_sample(),240},recovery_provider("adsb-ppm-crc24"),Json::object(),budget);}
}
int main(){const auto result=run(false);require(result.frames.size()==1 && result.frames[0].state==RecoveryFrameState::validated);require(result.sequences[0].values.size()==14 && result.sequences[0].values[0].value==0x8d);
    const auto support=recovery_input_ranges(result,0,0,1);require(support.size()==1 && support[0].first==(1ULL<<50)+16 && support[0].count==16);
    require(run(true).frames.empty());
    for(unsigned scenario=0;scenario<3;++scenario){RecoveryBudget budget;if(scenario==0)budget.maximum_frames=0;else if(scenario==1)budget.workspace_payload_bytes=1;else budget.maximum_operations=1;
        bool rejected=false;try{run(false,budget);}catch(const std::length_error&){rejected=true;}require(rejected);}
    std::cout<<"ADS-B recovery source support, gap and budget contract passed\n";}
