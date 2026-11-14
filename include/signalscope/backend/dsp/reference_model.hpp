#pragma once
#include "signalscope/backend/dsp/waveform.hpp"
#include <functional>
#include <stdexcept>
#include <string_view>

namespace signalscope {
inline constexpr unsigned referenceModelProtocolVersion=3;
struct ReferenceModelContext {
    std::uint32_t sample_rate=0;
    SignalSampleRange range;
    double minimum_time_seconds=0,maximum_time_seconds=0;
    std::size_t workspace_payload_bytes=64*1024;
};
struct ReferenceModelPoint {
    std::uint64_t sample_offset=0; // Integer difference from the selected origin.
    std::uint32_t sample_rate=0;
    double time_offset_seconds=0;
    // v3 providers receive the search's integer delay separately. Older
    // providers retain the combined time_offset_seconds calling convention.
    std::int32_t integer_offset_samples=0;
    long double offset_samples() const {return static_cast<long double>(sample_offset)+integer_offset_samples;}
    long double seconds() const {return offset_samples()/sample_rate+time_offset_seconds;}
};
struct PreparedReferenceModel {
    Json parameters=Json::object(),method=Json::object(),issues=Json::array();
    // Pure, deterministic, finite reference function. It generates a model;
    // neither this function nor a good residual establishes signal identity.
    std::function<std::complex<double>(const ReferenceModelPoint&)> evaluate;
    std::size_t workspace_payload_bytes=0;
};
struct ReferenceModelProvider {
    std::string id,resource_namespace,label_key;
    unsigned method_version=1,protocol_version=1;
    std::function<Json(const ReferenceModelContext&)> parameters;
    std::function<PreparedReferenceModel(const ReferenceModelContext&,const Json&)> prepare;
    // v2: descriptions/limits depend on the entire pending configuration.
    // v1's context-only descriptors remain a compatible, narrower capability.
    // v3 keeps v2 descriptors/workspace and receives an exact integer delay in
    // ReferenceModelPoint, independently of the manual fractional time shift.
    std::function<Json(const ReferenceModelContext&,const Json&)> describe;
    std::string description_key;
};
// Static C++ composition, not a dynamically installable binary ABI.
std::vector<ReferenceModelProvider> reference_model_providers();
ReferenceModelProvider reference_model_provider(std::string_view);
void register_reference_model(ReferenceModelProvider);
bool supports_reference_model(const ReferenceModelProvider&);
Json reference_model_parameters(const ReferenceModelProvider&,const ReferenceModelContext&,const Json& pending=Json::object());
class ReferenceModelError:public std::runtime_error {
public:
    ReferenceModelError(std::string resources,Json problems):std::runtime_error("Invalid reference model parameters"),resource_namespace(std::move(resources)),issues(std::move(problems)){}
    std::string resource_namespace;
    Json issues;
};
struct ReferenceFitOptions {
    bool fit_complex_gain=true,fit_dc_offset=false;
    double time_offset_seconds=0;
    std::int32_t minimum_delay_samples=0,maximum_delay_samples=0;
    unsigned maximum_delay_candidates=257,minimum_observations=4;
    double delay_tie_power_fraction=1e-12;
    double maximum_duration_seconds=600;
    std::uint64_t maximum_model_evaluations=32'000'000,maximum_span_visits=16'000'000;
    std::size_t result_payload_bytes=16*1024*1024,workspace_payload_bytes=64*1024;
};
struct ReferenceComparison {
    std::shared_ptr<const WaveformAnalysis> analysis;
    SignalSampleRange range;
    std::string provider_id;
    unsigned method_version=0;
    Json parameters,method;
    ReferenceFitOptions options;
    std::uint64_t observations=0;
    std::int32_t delay_samples=0;
    std::vector<std::int32_t> tied_delays;
    std::complex<double> gain{1,0},dc_offset{};
    double input_mean_power=0,residual_rms=0;
    std::optional<double> normalized_rms_error;
    // Packed observed subset on the unchanged parent clock (trace protocol v4).
    // These are generated reference/residual values, never new observations.
    std::vector<AnalysisTrace> traces;
    const AnalysisTrace& trace(std::string_view) const;
    std::size_t payload_bytes() const;
};
ReferenceComparison compare_reference_model(std::shared_ptr<const WaveformAnalysis>,SignalSampleRange,
    const ReferenceModelProvider&,const Json&,const ReferenceFitOptions& options={},std::stop_token stop={});
}
