#pragma once
#include "codec.hpp"

namespace signalscope::recovery::lora {
struct DemodOptions {
    PacketOptions packet;
    unsigned bandwidth_hz=125000,data_offset_samples=0,reference_offset_samples=0;
    unsigned maximum_symbols=64,fft_padding=4,timing_radius_samples=0,timing_step_samples=1;
    bool use_reference=false,inverted_iq=false;
    double carrier_offset_hz=0,minimum_rms=.0001,minimum_concentration=.02;
    double drift_min=0,drift_max=0,drift_step=.002,bias_min=0,bias_max=0,bias_step=.125;
};
struct Peak {
    SignalSampleRange support;double bin=0,concentration=0,rms=0;
    bool known=false;std::string issue_key;
};
struct Hypothesis {
    std::array<std::uint8_t,257> bytes{},known{};
    std::array<unsigned,6> score{};
    std::array<unsigned,4> domain_score{};
    unsigned byte_count=0,peak_first=0,symbol_count=0,matches=0;
    int shift=0;double drift=0,bias=0;
};
struct Demodulation {
    DemodOptions options;
    std::vector<Peak> peaks;
    std::vector<Hypothesis> candidates;
    unsigned chips=0,symbol_samples=0,fft_size=0,chosen_peak_first=0,chosen_symbols=0,attempts=0;
    int chosen_shift=0;double chosen_drift=0,chosen_bias=0;
    bool candidate_limit_reached=false,best_candidates_complete=true,domain_best_complete=true,domain_ranked=false;
    std::uint64_t operations=0;std::size_t workspace_payload_bytes=0;
};
struct CandidatePriority {std::array<unsigned,4> score{};bool retain=true;};
struct CandidatePolicy {
    std::size_t workspace_payload_bytes=0;
    std::uint64_t work_per_packet=0;
    std::function<CandidatePriority(const Packet&,std::stop_token)> evaluate;
};
Demodulation demodulate(const RecoveryContext&,const DemodOptions&,std::stop_token stop={},const CandidatePolicy* policy=nullptr);
std::vector<RecoveryValue> hypothesis_symbols(const Demodulation&,const Hypothesis&);
RecoveryData demodulation_evidence(const RecoveryContext&,Demodulation,std::stop_token stop={},const Hypothesis* representative=nullptr);
}
