#include "lora_tx_truth.hpp"
#include "signalscope/backend/dsp/recovery_export.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <iostream>
#include <numbers>

using namespace signalscope;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
constexpr std::uint64_t origin=(1ULL<<54)+601;
RecoveryResult recover(const independent_lora_truth::Encoded& tx,unsigned body,bool repair=false,Json extra=Json::object(),RecoveryBudget budget={}) {
    auto source=independent_lora_truth::iq(tx,origin);auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));const auto chips=1u<<tx.options.sf;
    Json parameters={{"sf",tx.options.sf},{"bandwidth_hz",500000},{"data_offset_samples",32+chips*25/4},{"reference_offset_samples",32},{"use_reference",true},{"maximum_symbols",128},{"paired_body_bytes",body},{"paired_repair",repair}};parameters.update(extra);
    return recover_signal(analysis,{origin,source->sample_count()},recovery_provider("bounded-lora-paired-crc16"),parameters,budget,{{"future_event",{{"retained",true}}}});
}
const RecoveryFrame& frame(const RecoveryResult& result,const char* id){const auto found=std::find_if(result.frames.begin(),result.frames.end(),[&](const auto& value){return value.id==id;});if(found==result.frames.end())throw std::runtime_error("Application frame evidence absent: "+result.method.dump());return *found;}
void matches(const RecoveryResult& result,const char* sequence,const std::vector<unsigned>& expected){const auto found=std::find_if(result.sequences.begin(),result.sequences.end(),[&](const auto& value){return value.id==sequence;});if(found==result.sequences.end() || found->values.size()!=expected.size())throw std::runtime_error(std::string("Application sequence extent differs from transmit truth: ")+sequence+" actual="+(found==result.sequences.end()?"absent":std::to_string(found->values.size()))+" expected="+std::to_string(expected.size()));for(unsigned i=0;i<expected.size();++i)require(found->values[i].known && found->values[i].value==expected[i],"Application bytes differ from independent transmit truth");}
unsigned reverse(unsigned value,unsigned count){unsigned result=0;for(unsigned i=0;i<count;++i)result=(result<<1)|((value>>i)&1);return result;}
std::vector<unsigned> paired(const std::vector<unsigned>& inner) {
    std::vector<unsigned> bytes;for(unsigned byte:inner){unsigned high=0,low=0;for(int bit=7;bit>=4;--bit)high=(high<<2)|(((byte>>bit)&1)?2:1);for(int bit=3;bit>=0;--bit)low=(low<<2)|(((byte>>bit)&1)?2:1);bytes.push_back(high);bytes.push_back(low);}
    // Whole GF(2) long division, independent of the receive CRC recurrence.
    std::vector<bool> polynomial;for(auto byte:bytes)for(unsigned bit=0;bit<8;++bit)polynomial.push_back((byte>>bit)&1);const auto count=polynomial.size();polynomial.resize(count+16);const auto initial=reverse(0x0399,16);for(unsigned i=0;i<16;++i)polynomial[i]=polynomial[i]^((initial>>(15-i))&1);for(unsigned i=0;i<count;++i)if(polynomial[i])for(unsigned power:{0,4,11,16})polynomial[i+power]=!polynomial[i+power];unsigned check=0;for(auto i=count;i<polynomial.size();++i)check=(check<<1)|polynomial[i];check=reverse(check,16);bytes.push_back(check>>8);bytes.push_back(check&255);return bytes;
}
void publicExample() {
    const std::vector<unsigned> wire{0x65,0x59,0x55,0x69,0x55,0x56,0x55,0x5a,0x96,0xa5,0x99,0xa6,0x69,0x9a,0xf5,0xd1},inner{0x42,0x06,0x01,0x03,0x9c,0xad,0x6b};
    require(paired(inner)==wire,"Independent paired transmitter disagrees with public UART example");independent_lora_truth::PacketOptions options;options.sf=7;const auto tx=independent_lora_truth::encode(wire,options);const auto result=recover(tx,14);matches(result,"paired-wire",wire);matches(result,"paired-inner",inner);
    require(frame(result,"paired-envelope").state==RecoveryFrameState::validated && result.frames[0].state==RecoveryFrameState::candidate && result.method.at("application_strict")==true,"Absent PHY CRC and independently validated application scope were conflated");
    for(const char* gate:{"minimum_rms","minimum_concentration"}){const auto relaxed=recover(tx,14,false,{{gate,0}});matches(relaxed,"paired-inner",inner);require(frame(relaxed,"paired-envelope").state==RecoveryFrameState::candidate && relaxed.method.at("application_observation_gates_enabled")==false,"Disabling an observation admission gate silently retained strict validation");}
    const auto ranges=recovery_input_ranges(result,11,0,1);require(ranges.size()==2 && ranges[0]==SignalSampleRange{origin+32,128} && ranges[1].first==origin+832,"Application selection provenance lost reference, full packet or large source clock");
    auto weak=tx;independent_lora_truth::flip(weak,7,4);const auto observed=recover(weak,14);matches(observed,"paired-inner",inner);require(frame(observed,"paired-envelope").state==RecoveryFrameState::candidate && observed.method.at("application_strict")==false,"A parity-failed unchanged payload was promoted to strict application validation");
    Json descriptions=recovery_provider("bounded-lora-paired-crc16").describe({result.analysis,result.range,{},nullptr},result.parameters);TextCatalog::resource(result.resource_namespace).resolve(descriptions);for(const auto& field:descriptions)require(field.at("label").contains("en") && field.at("label").contains("zh-CN"),"Application provider failed to distribute shared or own parameter resources");
    std::string exported;export_recovery_json(result,Json::object(),[&](auto text){exported.append(text);});const auto document=Json::parse(exported);require(!document.empty() && result.event.at("future_event").at("retained")==true,"Application evidence export or unknown event retention failed");
}
void repairsAndFailures() {
    std::vector<unsigned> inner(24);unsigned sum=0;for(unsigned i=0;i<23;++i){inner[i]=(i*19+11)&255;sum+=inner[i];}inner[23]=(-sum)&255;const auto wire=paired(inner);independent_lora_truth::PacketOptions options;options.sf=11;const auto good=independent_lora_truth::encode(wire,options);const auto result=recover(good,48,false,{{"paired_payload_xor",0x6d},{"paired_status_fields",true}});matches(result,"paired-inner",inner);require(frame(result,"paired-inner").fields.contains("layout_hypotheses") && result.method.at("application_business_identified")==false,"Manual field hypotheses became business identification or disappeared");
    auto damaged=wire;damaged[2]&=63;damaged[9]&=63;const auto tx=independent_lora_truth::encode(damaged,options);const auto unrepaired=recover(tx,48);require(frame(unrepaired,"paired-envelope").state==RecoveryFrameState::invalid && unrepaired.method.at("application_solutions").empty(),"Invalid pairs acquired a valid application frame without repair");
    const auto fixed=recover(tx,48,true);matches(fixed,"paired-wire",wire);matches(fixed,"paired-inner",inner);require(fixed.method.at("application_unique")==true && fixed.method.at("application_repaired")==true && frame(fixed,"paired-envelope").state==RecoveryFrameState::candidate && frame(fixed,"paired-envelope").fields.at("repairs").size()==2,"Unique bounded repair lost its weak status or changes");matches(fixed,"payload-bytes",damaged);
    damaged=wire;damaged.back()^=1;const auto badCrc=recover(independent_lora_truth::encode(damaged,options),48,true);require(frame(badCrc,"paired-envelope").state==RecoveryFrameState::invalid && badCrc.method.at("application_solutions").empty(),"Repair modified observed CRC bits");
    auto badSum=inner;badSum.back()^=1;const auto summed=recover(independent_lora_truth::encode(paired(badSum),options),48);require(frame(summed,"paired-envelope").state==RecoveryFrameState::invalid,"A known failed inner sum remained an unconstrained candidate despite a passing outer CRC");
    rejected([&]{recover(good,47);},"Odd paired body length was accepted");rejected([&]{recover(good,48,false,{{"paired_body_bytes",14},{"paired_status_fields",true}});},"Status layout accepted the wrong full pending body size");
    RecoveryBudget budget;budget.maximum_operations=1;rejected([&]{recover(good,48,true,Json::object(),budget);},"Application provider ignored work admission");budget={};budget.workspace_payload_bytes=1;rejected([&]{recover(good,48,true,Json::object(),budget);},"Application provider ignored workspace admission");
}
}
int main(){try{publicExample();repairsAndFailures();std::cout<<"Independent LoRa I/Q to paired CRC/sum frames, strict/weak/repaired states, public bytes, raw observations, source provenance, resources, export and full pending parameters passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
