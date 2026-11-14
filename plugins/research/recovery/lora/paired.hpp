#pragma once
#include "demod.hpp"

namespace signalscope::recovery::lora {
struct PairedOptions {
    unsigned body_bytes=48,zero_pair=1,maximum_erasure_pairs=12,maximum_solutions=16;
    std::uint16_t crc_initial=0x0399,crc_xorout=0;
    bool repair=false;
    std::uint64_t maximum_trials=131072;
};
struct PairInspection {
    std::array<std::uint8_t,124> inner{},known{};
    unsigned inner_count=0,invalid_pairs=0,unknown_pairs=0;
    bool complete=false;
    RecoveryCheckState crc=RecoveryCheckState::unknown,line_code=RecoveryCheckState::unknown,sum=RecoveryCheckState::unknown;
    std::uint16_t calculated_crc=0,received_crc=0;
    std::uint64_t operations=0;
    bool valid() const{return complete && crc==RecoveryCheckState::pass && line_code==RecoveryCheckState::pass && sum==RecoveryCheckState::pass;}
};
struct RepairedPair {
    unsigned wire_byte=0,shift=0,received=0,decided=0;bool originally_known=false;
};
struct PairSolution {
    std::array<std::uint8_t,250> wire{};
    std::array<std::uint8_t,124> inner{};
    std::array<RepairedPair,16> repairs{};
    unsigned wire_count=0,inner_count=0,repair_count=0;
};
struct PairRecovery {
    PairInspection raw;
    std::vector<PairSolution> solutions;
    std::uint64_t operations=0,trials=0;
    unsigned distinct_solutions=0;
    bool solutions_complete=true;
    std::size_t workspace_payload_bytes() const;
};
PairInspection inspect_paired(std::span<const RecoveryValue>,const PairedOptions&,std::uint64_t maximum_operations,std::stop_token={});
PairRecovery recover_paired(std::span<const RecoveryValue>,const PairedOptions&,const RecoveryBudget& budget={},std::stop_token stop={});
}
