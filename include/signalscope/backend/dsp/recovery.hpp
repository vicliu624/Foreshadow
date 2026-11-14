#pragma once
#include "signalscope/backend/dsp/waveform.hpp"
#include <functional>
#include <stdexcept>
#include <string_view>

namespace signalscope {
inline constexpr unsigned researchRecoveryProtocolVersion=2;
enum class RecoveryKind {digital,measurement,unsupported};
enum class RecoveryValueKind {symbol,bit,byte,complex};
enum class RecoveryCheckState {unknown,not_applicable,absent,pass,fail};
enum class RecoveryFrameState {candidate,validated,invalid,truncated};
struct RecoveryBudget {
    std::size_t result_payload_bytes=8*1024*1024,workspace_payload_bytes=1024*1024,metadata_bytes=64*1024;
    std::size_t maximum_values=131072,maximum_links=262144,maximum_frames=4096;
    std::uint64_t maximum_input_positions=1'000'000,maximum_operations=64'000'000;
};
struct RecoveryLink {std::uint32_t sequence=0,first=0,count=0;};
struct RecoveryValue {
    std::uint64_t value=0;bool known=false;
    // Requested support on the actual analysis clock. It can contain absent
    // observations; known=false and issue_key preserve that distinction.
    SignalSampleRange support;
    double metric=0; // Meaning/units belong to the stage, not a probability.
    std::string issue_key;
    // Earlier sequences only. FEC/deinterleaving can reference several ranges.
    std::vector<RecoveryLink> inputs;
    // v2: actual measured (or explicitly generated reference) complex point.
    std::complex<double> complex_value{};
    std::uint32_t group=0;
    std::int32_t coordinate=0;
};
struct RecoverySequence {
    std::string id,label_key;std::uint32_t stage=0;
    RecoveryValueKind kind=RecoveryValueKind::symbol;
    std::vector<RecoveryValue> values;
    // Resource key describing the measurement plane; empty for v1 sequences.
    std::string domain_key;
    bool reference=false;
};
struct RecoveryStage {
    std::string id,label_key;
    std::vector<std::uint32_t> inputs;
    Json parameters=Json::object(),features=Json::object(),conclusion=Json::object();
};
struct RecoveryCheck {
    std::string id,label_key;std::uint32_t stage=0;
    RecoveryCheckState state=RecoveryCheckState::unknown;bool required=false;
    Json evidence=Json::object();
};
struct RecoveryFrame {
    std::string id,label_key,validation_scope_key;
    std::uint32_t sequence=0,first=0,count=0;
    RecoveryFrameState state=RecoveryFrameState::candidate;
    std::vector<RecoveryCheck> checks;
    Json fields=Json::object();
};
struct RecoveryMetric {
    std::string id,label_key,unit_key;
    double value=0;
    bool available=false;
    std::string reason_key;
};
struct RecoveryData {
    RecoveryKind kind=RecoveryKind::unsupported;
    Json parameters=Json::object(),method=Json::object(),issues=Json::array();
    std::vector<RecoveryStage> stages;
    std::vector<RecoverySequence> sequences;
    std::vector<RecoveryFrame> frames;
    std::vector<RecoveryMetric> metrics; // v2: generic scalar measurements
    std::size_t workspace_payload_bytes=0;
    std::uint64_t operations=0;
    // Capacity-derived structured payload; JSON has a separate wire-size and
    // node/depth budget. This is not a measurement of JSON allocator overhead.
    std::size_t payload_bytes() const;
};
struct RecoveryContext {
    std::shared_ptr<const WaveformAnalysis> analysis;
    SignalSampleRange range;RecoveryBudget budget;
    const Json* event=nullptr;
};
struct RecoveryProvider {
    std::string id,resource_namespace,label_key,description_key;
    unsigned protocol_version=1,method_version=1;
    std::function<Json(const RecoveryContext&,const Json&)> describe;
    std::function<RecoveryData(const RecoveryContext&,const Json&,std::stop_token)> recover;
};
bool supports_recovery(const RecoveryProvider&);
std::vector<RecoveryProvider> recovery_providers();
RecoveryProvider recovery_provider(std::string_view);
void register_recovery(RecoveryProvider);
class RecoveryParameterError:public std::runtime_error {
public:
    RecoveryParameterError(std::string resources,Json problems):std::runtime_error("Invalid recovery parameters"),resource_namespace(std::move(resources)),issues(std::move(problems)){}
    std::string resource_namespace;Json issues;
};
struct RecoveryResult:RecoveryData {
    std::shared_ptr<const WaveformAnalysis> analysis;
    SignalSampleRange range;
    std::string provider_id,resource_namespace;
    unsigned method_version=0,protocol_version=1;
    Json event=Json::object();
};
RecoveryResult recover_signal(std::shared_ptr<const WaveformAnalysis>,SignalSampleRange,const RecoveryProvider&,const Json&,
    const RecoveryBudget& budget={},Json event=Json::object(),std::stop_token stop={});
void validate_recovery(const RecoveryResult&,const RecoveryBudget& budget={});
// Traverse byte/bit/FEC dependencies to the requested input support. Gaps are
// still gaps in the parent analysis; these ranges do not invent observations.
std::vector<SignalSampleRange> recovery_input_ranges(const RecoveryResult&,std::uint32_t sequence,std::uint32_t first,std::uint32_t count,
    std::size_t maximum_visits=262144,std::size_t maximum_ranges=4096);
}
