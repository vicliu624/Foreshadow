#include "plugins/research/recovery/lora/paired.hpp"
#include <iostream>
#include <map>
#include <set>

using namespace signalscope;
using namespace signalscope::recovery::lora;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
// Independent whole-polynomial division with reflected input/output. This
// does not use the production CRC shift register or repair syndromes.
unsigned reverse(unsigned value,unsigned width){unsigned result=0;for(unsigned i=0;i<width;++i)result=(result<<1)|((value>>i)&1);return result;}
unsigned oracleCrc(std::span<const std::uint8_t> data,const PairedOptions& options) {
    std::vector<bool> polynomial;for(const auto byte:data)for(unsigned bit=0;bit<8;++bit)polynomial.push_back((byte>>bit)&1);const auto size=polynomial.size();polynomial.resize(size+16);const auto initial=reverse(options.crc_initial,16);for(unsigned i=0;i<16;++i)polynomial[i]=polynomial[i]^((initial>>(15-i))&1);
    for(unsigned i=0;i<size;++i)if(polynomial[i])for(const auto offset:{0,4,11,16})polynomial[i+offset]=!polynomial[i+offset];unsigned value=0;for(auto i=size;i<polynomial.size();++i)value=(value<<1)|polynomial[i];return reverse(value,16)^options.crc_xorout;
}
std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& inner,const PairedOptions& options) {
    std::vector<std::uint8_t> result;unsigned word=0,position=0;for(const auto byte:inner)for(int bit=7;bit>=0;--bit){word=(word<<2)|(((byte>>bit)&1)?3-options.zero_pair:options.zero_pair);if(++position%4==0){result.push_back(static_cast<std::uint8_t>(word));word=0;}}
    const auto crc=oracleCrc(result,options);result.push_back(crc>>8);result.push_back(crc&255);return result;
}
std::vector<RecoveryValue> observed(const std::vector<std::uint8_t>& bytes){std::vector<RecoveryValue> result;for(const auto byte:bytes){RecoveryValue value;value.value=byte;value.known=true;value.support={200,1};result.push_back(value);}return result;}
std::string key(std::span<const std::uint8_t> bytes){return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
void examples() {
    const std::vector<std::uint8_t> publicExample{0x65,0x59,0x55,0x69,0x55,0x56,0x55,0x5a,0x96,0xa5,0x99,0xa6,0x69,0x9a,0xf5,0xd1};PairedOptions options;options.body_bytes=14;require(oracleCrc(std::span(publicExample).first(14),options)==0xf5d1,"Independent polynomial division failed the cached public UART example");const auto result=recover_paired(observed(publicExample),options);const std::array<std::uint8_t,7> inner{0x42,0x06,0x01,0x03,0x9c,0xad,0x6b};require(result.raw.valid() && result.solutions.size()==1 && std::equal(inner.begin(),inner.end(),result.solutions[0].inner.begin()),"Public independent UART/CRC/sum relation did not decode");
    for(unsigned polarity:{1,2}){options.body_bytes=48;options.zero_pair=polarity;std::vector<std::uint8_t> bytes(24);unsigned sum=0;for(unsigned i=0;i<23;++i){bytes[i]=static_cast<std::uint8_t>(i*17+9);sum+=bytes[i];}bytes[23]=static_cast<std::uint8_t>(-sum);const auto encoded=encode(bytes,options);const auto decoded=recover_paired(observed(encoded),options);require(decoded.raw.valid() && decoded.solutions[0].repair_count==0 && std::equal(bytes.begin(),bytes.end(),decoded.solutions[0].inner.begin()),"Explicit pair polarity or 24-byte complement sum was not preserved");}
}
void repair() {
    PairedOptions options;options.repair=true;std::vector<std::uint8_t> inner(24);unsigned sum=0;for(unsigned i=0;i<23;++i){inner[i]=static_cast<std::uint8_t>(31*i+7);sum+=inner[i];}inner[23]=static_cast<std::uint8_t>(-sum);const auto encoded=encode(inner,options);
    for(unsigned erasures:{1,3,8,12}){auto raw=observed(encoded);for(unsigned i=0;i<erasures;++i)raw[i].value&=63;const auto result=recover_paired(raw,options);require(result.raw.line_code==RecoveryCheckState::fail && result.distinct_solutions==1 && result.solutions_complete && result.trials==(1u<<erasures),"Finite pair repair failed or hid the invalid received pairs");const auto& repaired=result.solutions[0];require(std::equal(encoded.begin(),encoded.end(),repaired.wire.begin()) && repaired.repair_count==erasures && std::equal(inner.begin(),inner.end(),repaired.inner.begin()),"CRC/sum-constrained repair differs from independent transmit truth");require(result.operations<200000,"Syndrome repair recomputed a full CRC per assignment");
        if(erasures<=8){std::set<std::string> brute;for(unsigned trial=0;trial<(1u<<erasures);++trial){auto candidate=encoded;for(unsigned i=0;i<erasures;++i)candidate[i]=static_cast<std::uint8_t>((candidate[i]&63)|(((trial>>i)&1)?128:64));if(oracleCrc(std::span(candidate).first(48),options)!=((unsigned(candidate[48])<<8)|candidate[49]))continue;const auto inspected=inspect_paired(observed(candidate),options,10000);if(inspected.sum==RecoveryCheckState::pass)brute.insert(key(candidate));}require(brute.size()==result.distinct_solutions && brute.contains(key(std::span(repaired.wire).first(repaired.wire_count))),"Gray-code syndrome search differs from independent exhaustive polynomial verification");}
    }
    auto unknown=observed(encoded);unknown[2].known=false;const auto recovered=recover_paired(unknown,options);require(recovered.solutions.size()==1 && recovered.solutions[0].repair_count==4 && !recovered.solutions[0].repairs[0].originally_known,"An unknown byte was silently treated as four known pairs");
    auto changed=observed(encoded);changed[2].value^=3u<<6;require(recover_paired(changed,options).solutions.empty(),"Repair changed a legal observed pair to force a checksum match");changed=observed(encoded);changed.back().known=false;require(recover_paired(changed,options).solutions.empty(),"Repair invented missing received CRC bits");
    auto shortInput=observed(encoded);shortInput.pop_back();require(!recover_paired(shortInput,options).raw.complete && recover_paired(shortInput,options).solutions.empty(),"Truncated content became a complete frame");
}
void ambiguous() {
    PairedOptions options;options.body_bytes=6;options.repair=true;options.maximum_erasure_pairs=16;std::map<unsigned,std::vector<std::uint8_t>> first;std::vector<std::uint8_t> a,b;
    for(unsigned x=0;x<65536 && a.empty();++x){const auto hi=static_cast<std::uint8_t>(x>>8),lo=static_cast<std::uint8_t>(x);const auto candidate=encode({hi,lo,static_cast<std::uint8_t>(-unsigned(hi)-lo)},options);const auto crc=(unsigned(candidate[6])<<8)|candidate[7];if(const auto it=first.find(crc);it!=first.end()){unsigned differences=0;for(unsigned i=0;i<6;++i)for(unsigned shift:{6,4,2,0})differences+=((candidate[i]>>shift)&3)!=((it->second[i]>>shift)&3);if(differences<=16){a=it->second;b=candidate;}}else first.emplace(crc,candidate);}
    require(!a.empty(),"No independent ambiguous CRC/complement fixture found");auto raw=observed(a);for(unsigned i=0;i<6;++i)for(unsigned shift:{6,4,2,0})if(((a[i]>>shift)&3)!=((b[i]>>shift)&3))raw[i].value&=~(3u<<shift);const auto result=recover_paired(raw,options);require(result.distinct_solutions>=2 && result.solutions_complete,"Two valid checksum completions were collapsed to one");options.maximum_solutions=1;const auto limited=recover_paired(raw,options);require(limited.solutions.size()==1 && limited.distinct_solutions>=2 && !limited.solutions_complete,"A retained-solution cap was reported as a unique repair");
}
void limits() {
    PairedOptions options;options.repair=true;std::vector<std::uint8_t> inner(24);const auto encoded=encode(inner,options);auto input=observed(encoded);input[0].value&=63;
    RecoveryBudget budget;budget.maximum_operations=1;rejected([&]{recover_paired(input,options,budget);},"Pair repair escaped its operation budget");budget=RecoveryBudget{};budget.workspace_payload_bytes=1;rejected([&]{recover_paired(input,options,budget);},"Pair repair escaped its workspace budget");options.maximum_trials=1;rejected([&]{recover_paired(input,options);},"Pair repair escaped its trial budget");options.maximum_trials=131072;options.maximum_erasure_pairs=0;require(recover_paired(input,options).solutions.empty(),"Pair repair exceeded its erasure bound");std::stop_source stop;stop.request_stop();rejected([&]{recover_paired(input,options,{},stop.get_token());},"Canceled pair repair produced a result");
}
}
int main(){try{examples();repair();ambiguous();limits();std::cout<<"Paired-code CRC16/complement profile: independent public example, polynomial truth, finite syndrome repair, ambiguity, unknown/truncated data and budgets passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
