#include "paired.hpp"
#include <algorithm>
#include <bit>

namespace signalscope::recovery::lora {
namespace {
struct Work {
    std::uint64_t used=0,maximum;std::stop_token stop;
    void add(std::uint64_t amount){if(stop.stop_requested())throw std::runtime_error("Paired-code recovery canceled");if(amount>maximum-used)throw std::length_error("Paired-code work limit exceeded");used+=amount;}
};
void validate(const PairedOptions& v){if(v.body_bytes<2 || v.body_bytes>248 || v.body_bytes%2 || (v.zero_pair!=1 && v.zero_pair!=2) || v.maximum_erasure_pairs>16 || !v.maximum_solutions || v.maximum_solutions>16 || !v.maximum_trials)throw std::invalid_argument("Invalid paired-code research profile");}
std::uint16_t crc(std::span<const std::uint8_t> bytes,const PairedOptions& options,Work& work){auto value=options.crc_initial;for(const auto byte:bytes){value^=byte;for(unsigned bit=0;bit<8;++bit){work.add(1);value=static_cast<std::uint16_t>((value>>1)^((value&1)?0x8408:0));}}return value^options.crc_xorout;}
PairInspection inspect(std::span<const RecoveryValue> input,const PairedOptions& options,Work& work) {
    PairInspection result;result.inner_count=options.body_bytes/2;result.complete=input.size()==options.body_bytes+2;std::array<std::uint8_t,250> bytes{};bool allKnown=result.complete;
    for(unsigned i=0;i<std::min<std::size_t>(input.size(),options.body_bytes+2);++i){if(input[i].value>255)throw std::invalid_argument("Paired-code input is not a byte");bytes[i]=static_cast<std::uint8_t>(input[i].value);allKnown=allKnown && input[i].known;}
    std::fill_n(result.known.begin(),result.inner_count,1);
    for(unsigned i=0;i<options.body_bytes;++i)for(unsigned pair=0;pair<4;++pair){work.add(1);const auto position=i*4+pair,inner=position/8,mask=1u<<(7-position%8);const bool observed=i<input.size() && input[i].known;const auto value=static_cast<unsigned>((bytes[i]>>(6-pair*2))&3);const bool legal=value==1 || value==2;result.inner[inner]|=(value!=options.zero_pair && legal)?mask:0;if(!observed){++result.unknown_pairs;result.known[inner]=0;}else if(!legal){++result.invalid_pairs;result.known[inner]=0;}}
    result.line_code=result.invalid_pairs?RecoveryCheckState::fail:result.unknown_pairs?RecoveryCheckState::unknown:RecoveryCheckState::pass;
    if(result.complete){result.received_crc=static_cast<std::uint16_t>((std::uint16_t(bytes[options.body_bytes])<<8)|bytes[options.body_bytes+1]);if(allKnown){result.calculated_crc=crc(std::span(bytes).first(options.body_bytes),options,work);result.crc=result.calculated_crc==result.received_crc?RecoveryCheckState::pass:RecoveryCheckState::fail;}}
    if(std::all_of(result.known.begin(),result.known.begin()+result.inner_count,[](auto value){return value!=0;})){unsigned sum=0;for(unsigned i=0;i<result.inner_count;++i){sum+=result.inner[i];work.add(1);}result.sum=(sum&255)==0?RecoveryCheckState::pass:RecoveryCheckState::fail;}
    return result;
}
}
std::size_t PairRecovery::workspace_payload_bytes() const{return sizeof(PairRecovery)+solutions.capacity()*sizeof(PairSolution)+2*sizeof(PairSolution)+16*sizeof(std::uint16_t)+sizeof(Work);}
PairInspection inspect_paired(std::span<const RecoveryValue> input,const PairedOptions& options,std::uint64_t maximum,std::stop_token stop){validate(options);Work work{0,maximum,stop};work.add(1);auto result=inspect(input,options,work);result.operations=work.used;return result;}
PairRecovery recover_paired(std::span<const RecoveryValue> input,const PairedOptions& options,const RecoveryBudget& budget,std::stop_token stop) {
    validate(options);if(sizeof(PairRecovery)+options.maximum_solutions*sizeof(PairSolution)+2*sizeof(PairSolution)+16*sizeof(std::uint16_t)+sizeof(Work)>budget.workspace_payload_bytes)throw std::length_error("Paired-code repair workspace exceeds its budget");
    Work work{0,budget.maximum_operations,stop};work.add(1);PairRecovery result;result.solutions.reserve(options.maximum_solutions);result.raw=inspect(input,options,work);
    PairSolution current;current.wire_count=options.body_bytes+2;current.inner_count=options.body_bytes/2;
    for(unsigned i=0;i<std::min<std::size_t>(input.size(),current.wire_count);++i)current.wire[i]=static_cast<std::uint8_t>(input[i].value);
    current.inner=result.raw.inner;
    if(result.raw.valid()){result.solutions.push_back(current);result.distinct_solutions=1;result.operations=work.used;return result;}
    if(!options.repair || !result.raw.complete || !input[options.body_bytes].known || !input[options.body_bytes+1].known){result.operations=work.used;return result;}
    for(unsigned i=0;i<options.body_bytes;++i)for(unsigned pair=0;pair<4;++pair){const unsigned shift=6-pair*2,received=(current.wire[i]>>shift)&3;if(input[i].known && (received==1 || received==2))continue;if(current.repair_count>=options.maximum_erasure_pairs){result.operations=work.used;return result;}auto& repair=current.repairs[current.repair_count++];repair={i,shift,received,options.zero_pair,input[i].known};current.wire[i]=static_cast<std::uint8_t>((current.wire[i]&~(3u<<shift))|(options.zero_pair<<shift));}
    if(!current.repair_count){result.operations=work.used;return result;}
    const auto trials=std::uint64_t(1)<<current.repair_count;if(trials>options.maximum_trials)throw std::length_error("Paired-code repair trial limit exceeded");
    std::array<std::uint16_t,16> deltas{};auto state=crc(std::span(current.wire).first(options.body_bytes),options,work);const auto base=state;
    for(unsigned e=0;e<current.repair_count;++e){const auto& repair=current.repairs[e];current.wire[repair.wire_byte]^=3<<repair.shift;deltas[e]=crc(std::span(current.wire).first(options.body_bytes),options,work)^base;current.wire[repair.wire_byte]^=3<<repair.shift;}
    std::fill(current.inner.begin(),current.inner.end(),0);for(unsigned i=0;i<options.body_bytes;++i)for(unsigned pair=0;pair<4;++pair){const auto position=i*4+pair;current.inner[position/8]|=(((current.wire[i]>>(6-pair*2))&3)!=options.zero_pair)<<(7-position%8);work.add(1);}unsigned sum=0;for(unsigned i=0;i<current.inner_count;++i)sum+=current.inner[i];
    std::uint64_t previous=0;for(std::uint64_t trial=0;trial<trials;++trial){work.add(2);const auto gray=trial^(trial>>1);if(trial){const auto changed=static_cast<unsigned>(std::countr_zero(gray^previous));const auto& repair=current.repairs[changed];const auto position=repair.wire_byte*4+(6-repair.shift)/2,byte=position/8,mask=1u<<(7-position%8);const bool wasOne=current.inner[byte]&mask;sum=(sum+(wasOne?256-mask:mask))&255;current.inner[byte]^=mask;current.wire[repair.wire_byte]^=3<<repair.shift;state^=deltas[changed];}previous=gray;++result.trials;
        if(state!=result.raw.received_crc || (sum&255))continue;
        ++result.distinct_solutions;if(result.solutions.size()<options.maximum_solutions){auto solution=current;for(unsigned e=0;e<solution.repair_count;++e)solution.repairs[e].decided=(solution.wire[solution.repairs[e].wire_byte]>>solution.repairs[e].shift)&3;result.solutions.push_back(solution);}else result.solutions_complete=false;
    }
    result.operations=work.used;if(result.workspace_payload_bytes()>budget.workspace_payload_bytes)throw std::length_error("Allocated paired-code workspace exceeds its budget");return result;
}
}
