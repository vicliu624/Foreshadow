#include "signalscope/backend/dsp/reference_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace signalscope {
namespace {
bool finite(std::complex<double> value){return std::isfinite(value.real()) && std::isfinite(value.imag());}
struct Candidate {std::complex<double> gain{1,0},dc{};double error=0,power=0;};
}
const AnalysisTrace& ReferenceComparison::trace(std::string_view id) const {
    for(const auto& value:traces)if(value.id==id)return value;
    throw std::out_of_range("Missing model comparison trace");
}
std::size_t ReferenceComparison::payload_bytes() const {
    return tied_delays.capacity()*sizeof(std::int32_t)+analysis_traces_payload_bytes(traces);
}
ReferenceComparison compare_reference_model(std::shared_ptr<const WaveformAnalysis> analysis,SignalSampleRange range,
    const ReferenceModelProvider& provider,const Json& parameters,const ReferenceFitOptions& options,std::stop_token stop) {
    if(!analysis || (!analysis->source && !analysis->processed) || !analysis->sample_count() || !analysis->sample_rate() || !range.count ||
        range.first>UINT64_MAX-range.count || range.first<analysis->first_sample() || range.first+range.count>analysis->end_sample())throw std::invalid_argument("Invalid model comparison selection");
    if(!supports_reference_model(provider) || !parameters.is_object() || parameters.dump().size()>65536)throw std::invalid_argument("Invalid model comparison provider or parameters");
    const auto candidates=std::int64_t(options.maximum_delay_samples)-options.minimum_delay_samples+1;
    if(candidates<1 || candidates>options.maximum_delay_candidates || options.maximum_delay_candidates>4097 || options.minimum_observations<1 ||
        !std::isfinite(options.time_offset_seconds) || !std::isfinite(options.delay_tie_power_fraction) || options.delay_tie_power_fraction<0 || options.delay_tie_power_fraction>1e-3 || !std::isfinite(options.maximum_duration_seconds) || options.maximum_duration_seconds<=0 ||
        std::abs(options.time_offset_seconds)>options.maximum_duration_seconds || double(range.count)/analysis->sample_rate()>options.maximum_duration_seconds)throw std::invalid_argument("Invalid model alignment or duration budget");
    if(std::abs(options.time_offset_seconds+double(options.minimum_delay_samples)/analysis->sample_rate())>options.maximum_duration_seconds ||
        std::abs(options.time_offset_seconds+double(options.maximum_delay_samples)/analysis->sample_rate())>options.maximum_duration_seconds)throw std::invalid_argument("Model alignment exceeds its time budget");
    const auto passes=std::uint64_t(candidates)*2+1;
    if(analysis->spans.size()>options.maximum_span_visits/(passes+2))throw std::length_error("Model comparison exceeds its span visit budget");
    const auto count=analysis->sample_count();const auto& inphase=analysis->trace("i").values;const auto& quadrature=analysis->trace("q").values;
    validate_analysis_trace(*analysis,analysis->trace("i"));validate_analysis_trace(*analysis,analysis->trace("q"));
    if(count>SIZE_MAX/(4*sizeof(double)) || inphase.size()!=count || quadrature.size()!=count)throw std::invalid_argument("Model comparison traces disagree with their clock");
    const auto end=range.first+range.count;std::uint64_t observations=0,expected=0,previous=0;std::size_t mapped_spans=0;
    for(const auto& span:analysis->spans) {
        if(!span.count || span.first_sample>UINT64_MAX-span.count || span.first_value!=expected || span.count>count-expected || (expected && span.first_sample<previous))throw std::invalid_argument("Invalid model input spans");
        expected+=span.count;previous=span.first_sample+span.count;
        const auto first=std::max(range.first,span.first_sample),last=std::min(end,previous);if(first<last){observations+=last-first;++mapped_spans;}
    }
    if(expected!=count || observations<options.minimum_observations)throw std::invalid_argument("Insufficient observed samples for model comparison");
    if(observations>options.maximum_model_evaluations/passes)throw std::length_error("Model comparison exceeds its evaluation budget");
    const auto delay_bytes=std::size_t(candidates)*sizeof(std::int32_t);
    if(std::size_t(candidates)>options.workspace_payload_bytes/sizeof(Candidate) || delay_bytes>options.result_payload_bytes || mapped_spans>(options.result_payload_bytes-delay_bytes)/sizeof(AnalysisTraceSpan))throw std::length_error("Model comparison exceeds its payload budget");
    auto mapping=std::make_shared<std::vector<AnalysisTraceSpan>>();mapping->reserve(mapped_spans);
    if(mapping->capacity()>(options.result_payload_bytes-delay_bytes)/sizeof(AnalysisTraceSpan))throw std::length_error("Model mapping exceeds its payload budget");
    const auto mapping_bytes=mapping->capacity()*sizeof(AnalysisTraceSpan);
    if(observations>(options.result_payload_bytes-delay_bytes-mapping_bytes)/(4*sizeof(double)))throw std::length_error("Model traces exceed their payload budget");
    std::size_t packed=0;for(const auto& span:analysis->spans){const auto first=std::max(range.first,span.first_sample),last=std::min(end,span.first_sample+span.count);if(first<last){mapping->push_back({first,packed,static_cast<std::size_t>(last-first),first==span.first_sample && span.discontinuous_before});packed+=last-first;}}
    if(stop.stop_requested())throw std::runtime_error("Model comparison canceled");
    std::vector<Candidate> fits;fits.reserve(static_cast<std::size_t>(candidates));if(fits.capacity()>options.workspace_payload_bytes/sizeof(Candidate))throw std::length_error("Model alignment allocation exceeds its workspace budget");
    const auto fit_workspace=fits.capacity()*sizeof(Candidate);
    const ReferenceModelContext context{analysis->sample_rate(),range,options.time_offset_seconds+double(options.minimum_delay_samples)/analysis->sample_rate(),
        double(range.count-1)/analysis->sample_rate()+options.time_offset_seconds+double(options.maximum_delay_samples)/analysis->sample_rate(),options.workspace_payload_bytes-fit_workspace};auto prepared=provider.prepare(context,parameters);
    if(prepared.workspace_payload_bytes>context.workspace_payload_bytes)throw std::length_error("Reference provider exceeds its remaining workspace budget");
    if(!prepared.issues.is_array() || prepared.issues.dump().size()>65536 || !prepared.parameters.is_object() || !prepared.method.is_object() || prepared.parameters.dump().size()>65536 || prepared.method.dump().size()>65536)throw std::invalid_argument("Invalid prepared reference model");
    for(const auto& issue:prepared.issues)if(!issue.is_object() || !issue.contains("label_key") || !issue.at("label_key").is_string() || issue.at("label_key").get_ref<const std::string&>().empty())throw std::invalid_argument("Reference model issue lacks a resource key");
    if(!prepared.issues.empty())throw ReferenceModelError(provider.resource_namespace,std::move(prepared.issues));
    if(!prepared.evaluate)throw std::invalid_argument("Reference model has no generator");
    const auto each=[&](std::int32_t delay,auto consume) {
        const auto shift=options.time_offset_seconds+double(delay)/context.sample_rate;
        std::size_t output=0;
        for(const auto& span:analysis->spans) {
            if(stop.stop_requested())throw std::runtime_error("Model comparison canceled");
            const auto first=std::max(range.first,span.first_sample),last=std::min(end,span.first_sample+span.count);
            for(auto sample=first;sample<last;++sample) {
                if((sample&1023)==0 && stop.stop_requested())throw std::runtime_error("Model comparison canceled");
                const auto index=span.first_value+static_cast<std::size_t>(sample-span.first_sample);
                const std::complex<double> observed{inphase[index],quadrature[index]};
                const auto reference=prepared.evaluate(provider.protocol_version>=3?ReferenceModelPoint{sample-range.first,context.sample_rate,options.time_offset_seconds,delay}:ReferenceModelPoint{sample-range.first,context.sample_rate,shift});
                if(!finite(observed) || !finite(reference))throw std::invalid_argument("Non-finite model comparison sample");
                consume(output++,observed,reference);
            }
        }
    };
    const auto fit=[&](std::int32_t delay) {
        std::complex<long double> sum_x{},sum_y{},cross{};long double energy_x=0,energy_y=0;
        each(delay,[&](auto,std::complex<double> observed,std::complex<double> reference){const std::complex<long double> x=reference,y=observed;sum_x+=x;sum_y+=y;cross+=y*std::conj(x);energy_x+=std::norm(x);energy_y+=std::norm(y);});
        const long double n=observations;Candidate result;result.power=static_cast<double>(energy_y/n);
        if(options.fit_complex_gain) {
            const auto energy=options.fit_dc_offset?energy_x-std::norm(sum_x)/n:energy_x;
            if(!std::isfinite(energy) || energy<=std::numeric_limits<double>::epsilon()*std::max(energy_x,static_cast<long double>(std::numeric_limits<double>::min()))*32)throw std::invalid_argument("Reference gain or DC offset is not identifiable");
            result.gain=static_cast<std::complex<double>>((options.fit_dc_offset?cross-sum_y*std::conj(sum_x)/n:cross)/energy);
        }
        if(options.fit_dc_offset)result.dc=static_cast<std::complex<double>>(sum_y/n-std::complex<long double>(result.gain)*sum_x/n);
        if(!finite(result.gain) || !finite(result.dc) || !std::isfinite(result.power))throw std::invalid_argument("Non-finite model fit");
        long double error=0;each(delay,[&](auto,auto observed,auto reference){const auto residual=observed-result.gain*reference-result.dc;if(!finite(residual))throw std::invalid_argument("Non-finite model residual");error+=std::norm(std::complex<long double>(residual));});
        result.error=static_cast<double>(error/n);if(!std::isfinite(result.error))throw std::invalid_argument("Non-finite model error");return result;
    };
    ReferenceComparison result;result.analysis=analysis;result.range=range;result.provider_id=provider.id;result.method_version=provider.method_version;result.options=options;result.parameters=prepared.parameters;result.observations=observations;
    for(std::int64_t delay=options.minimum_delay_samples;delay<=options.maximum_delay_samples;++delay)fits.push_back(fit(static_cast<std::int32_t>(delay)));
    const auto minimum=std::min_element(fits.begin(),fits.end(),[](const auto& a,const auto& b){return a.error<b.error;});
    const auto tolerance=minimum->power*options.delay_tie_power_fraction;
    result.tied_delays.reserve(static_cast<std::size_t>(candidates));const Candidate* selected=nullptr;
    for(std::size_t i=0;i<fits.size();++i)if(fits[i].error<=minimum->error+tolerance) {
        const auto delay=static_cast<std::int32_t>(std::int64_t(options.minimum_delay_samples)+static_cast<std::int64_t>(i));result.tied_delays.push_back(delay);
        if(!selected || std::abs(std::int64_t(delay))<std::abs(std::int64_t(result.delay_samples))){selected=&fits[i];result.delay_samples=delay;}
    }
    if(!selected)throw std::runtime_error("Model alignment has no finite candidate");
    const auto& best=*selected;
    result.gain=best.gain;result.dc_offset=best.dc;result.input_mean_power=best.power;result.residual_rms=std::sqrt(best.error);if(best.power>0)result.normalized_rms_error=std::sqrt(best.error/best.power);
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    for(const auto* id:{"reference_i","reference_q","residual_i","residual_q"}){AnalysisTrace trace;trace.id=id;trace.label_key="model.trace."+trace.id;trace.unit="normalized-component";trace.mapping=mapping;trace.values.assign(static_cast<std::size_t>(observations),nan);result.traces.push_back(std::move(trace));}
    if(result.payload_bytes()>options.result_payload_bytes)throw std::length_error("Model comparison allocation exceeds its payload budget");
    each(result.delay_samples,[&](auto index,auto observed,auto reference){const auto aligned=result.gain*reference+result.dc_offset,residual=observed-aligned;result.traces[0].values[index]=aligned.real();result.traces[1].values[index]=aligned.imag();result.traces[2].values[index]=residual.real();result.traces[3].values[index]=residual.imag();});
    result.method={{"id","reference-comparison"},{"version",referenceModelProtocolVersion},{"provider",provider.id},{"provider_protocol_version",provider.protocol_version},{"provider_method_version",provider.method_version},{"provider_method",prepared.method},
        {"origin_analysis_sample",std::to_string(range.first)},{"sample_rate_hz",context.sample_rate},{"trace_protocol_version",analysisTraceProtocolVersion},{"boundary_policy","observed-points-only-no-gap-padding"},{"observations",std::to_string(observations)},
        {"model_evaluations",std::to_string(observations*passes)},{"provider_workspace_payload_bytes",provider.protocol_version>=2?Json(prepared.workspace_payload_bytes):Json()},
        {"provider_workspace_accounting",provider.protocol_version>=2?"declared":"legacy-not-declared"},{"delay_time_encoding",provider.protocol_version>=3?"separate-integer-samples-and-manual-seconds":"legacy-combined-seconds"},{"time_offset_seconds",options.time_offset_seconds},{"delay_samples",result.delay_samples},{"tied_delay_samples",result.tied_delays},{"delay_tie_power_fraction",options.delay_tie_power_fraction},
        {"delay_identifiability",candidates==1?"not-searched":result.tied_delays.size()>1?"ambiguous":"unique-within-declared-search"},{"fit_complex_gain",options.fit_complex_gain},{"fit_dc_offset",options.fit_dc_offset},
        {"gain",{{"real",result.gain.real()},{"imaginary",result.gain.imag()}}},{"dc_offset",{{"real",result.dc_offset.real()},{"imaginary",result.dc_offset.imag()}}},
        {"normalization","residual-rms/input-rms"},{"evm","not-established"},{"semantic_classification","not-performed"}};
    return result;
}
}
