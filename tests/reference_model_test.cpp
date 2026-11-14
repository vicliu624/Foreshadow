#include "signalscope/backend/dsp/reference_model.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
ReferenceModelProvider carrier() {
    ReferenceModelProvider provider;provider.id="independent.carrier";provider.resource_namespace="independent-model";provider.label_key="carrier";
    provider.parameters=[](const auto& context){return Json::array({{{"key","frequency_hz"},{"type","number"},{"extension",true},{"label_key","frequency"},{"minimum",-double(context.sample_rate)/2.},{"maximum",context.sample_rate/2.},{"default",173.25}}});};
    provider.prepare=[](const auto& context,const Json& parameters){PreparedReferenceModel model;model.parameters=parameters;
        const auto frequency=parameters.at("frequency_hz").template get<double>();
        if(!std::isfinite(frequency) || std::abs(frequency)>=context.sample_rate/2.){model.issues.push_back({{"field","frequency_hz"},{"label_key","frequency.range"},{"arguments",{{"rate",context.sample_rate}}}});return model;}
        model.method={{"fixture_future",{{"opaque",17}}}};model.evaluate=[frequency](const ReferenceModelPoint& point){return std::polar(1.,static_cast<double>(2*std::numbers::pi_v<long double>*frequency*point.seconds()));};return model;};return provider;
}
std::shared_ptr<const WaveformAnalysis> observations(bool chirp=false,bool zero=false) {
    auto source=std::make_shared<SignalWindow>();source->stream={4096,433000000,SampleFormat::cf32_le};source->source_id="independent.formula.truth";
    constexpr std::uint64_t origin=(1ULL<<54)+123;const std::complex<double> gain{.43,-.17},dc{.06,.04};
    for(const auto first:{0ULL,300ULL}) {
        SignalSegment segment;segment.first_sample=origin+first;segment.discontinuous_before=first!=0;
        for(unsigned n=0;n<256;++n) {
            const auto t=(double(first+n)+(chirp?3:0))/4096;
            const auto phase=2*std::numbers::pi*(chirp?35*t+200*t*t:173.25*t);
            const auto value=zero?std::complex<double>{}:gain*std::exp(std::complex<double>{0,phase})+dc;
            segment.samples.emplace_back(static_cast<float>(value.real()),static_cast<float>(value.imag()));
        }source->segments.push_back(std::move(segment));
    }
    return std::make_shared<WaveformAnalysis>(analyze_waveform(source));
}
void fitsAndClocks() {
    const auto provider=carrier();register_reference_model(provider);const auto discovered=reference_model_provider(provider.id);
    rejected([&]{register_reference_model(provider);},"Duplicate model registration accepted");auto unsupported=provider;unsupported.id="independent.future";unsupported.protocol_version=4;rejected([&]{register_reference_model(unsupported);},"Unsupported model protocol accepted");
    const auto analysis=observations();const SignalSampleRange range{analysis->first_sample(),analysis->end_sample()-analysis->first_sample()};
    ReferenceFitOptions options;options.fit_dc_offset=true;const auto result=compare_reference_model(analysis,range,discovered,{{"frequency_hz",173.25}},options);
    require(result.observations==512 && result.residual_rms<3e-8 && std::abs(result.gain-std::complex<double>{.43,-.17})<1e-7 && std::abs(result.dc_offset-std::complex<double>{.06,.04})<1e-7,"Known complex gain/DC or large-clock gap alignment was not recovered");
    require(result.trace("reference_i").values.size()==512 && result.method.at("provider_method").at("fixture_future").at("opaque")==17 && result.method.at("evm")=="not-established","Generated traces or future provider evidence were lost or falsely labeled EVM");
    const auto subset=compare_reference_model(analysis,{range.first+100,350},discovered,{{"frequency_hz",173.25}},options);
    const auto& compact=subset.trace("residual_i");validate_analysis_trace(*analysis,compact);
    require(subset.observations==306 && compact.values.size()==306 && !analysis_trace_value_index(*analysis,compact,range.first+50) && !analysis_trace_value_index(*analysis,compact,range.first+270) && analysis_trace_value_index(*analysis,compact,range.first+449)==305,"A partial model selection invented gap samples or compared outside observations");
    auto compactBudget=options;compactBudget.result_payload_bytes=4096;const auto tiny=compare_reference_model(analysis,{range.first+100,64},discovered,{{"frequency_hz",173.25}},compactBudget);
    require(tiny.payload_bytes()<4096 && tiny.trace("reference_i").values.size()==64,"A small model selection allocated the complete preview axis");
    const auto wrong=compare_reference_model(analysis,range,discovered,{{"frequency_hz",600.}},options);require(wrong.normalized_rms_error && *wrong.normalized_rms_error>.8,"An incorrect reference appeared to explain the observations");
    options.minimum_delay_samples=-3;options.maximum_delay_samples=3;const auto ambiguous=compare_reference_model(analysis,range,discovered,{{"frequency_hz",173.25}},options);
    require(ambiguous.tied_delays.size()==7 && ambiguous.delay_samples==0 && ambiguous.method.at("delay_identifiability")=="ambiguous","Free carrier phase produced fictitious unique timing");
    auto shifted=std::make_shared<SignalWindow>(*analysis->source);const auto delta=UINT64_MAX-600-range.first;for(auto& segment:shifted->segments)segment.first_sample+=delta;
    const auto nearLimit=std::make_shared<WaveformAnalysis>(analyze_waveform(shifted));options.minimum_delay_samples=options.maximum_delay_samples=0;
    const auto large=compare_reference_model(nearLimit,{nearLimit->first_sample(),nearLimit->end_sample()-nearLimit->first_sample()},discovered,{{"frequency_hz",173.25}},options);
    require(std::abs(large.gain-result.gain)<1e-12 && large.residual_rms<3e-8,"A near-UINT64_MAX source clock changed the model reference");
    ChannelExtractionOptions channel;channel.offset_hz=0;channel.passband_hz=1000;channel.transition_hz=200;channel.output_rate=1537;
    const auto processed=std::make_shared<ProcessedSignal>(extract_signal_channel(analysis->source,channel));const auto derived=std::make_shared<WaveformAnalysis>(analyze_waveform(processed));
    options.minimum_delay_samples=options.maximum_delay_samples=0;const auto rational=compare_reference_model(derived,{derived->first_sample(),derived->end_sample()-derived->first_sample()},discovered,{{"frequency_hz",173.25}},options);
    require(rational.residual_rms<2e-6 && rational.analysis->sample_rate()==1537 && rational.analysis->source_position(rational.range.first)==derived->source_position(derived->first_sample()),"The model used raw instead of rational processed clocks");
    const auto zeros=observations(false,true);const auto silent=compare_reference_model(zeros,{zeros->first_sample(),zeros->end_sample()-zeros->first_sample()},discovered,{{"frequency_hz",173.25}},options);
    require(!silent.normalized_rms_error && silent.residual_rms==0 && silent.method.at("semantic_classification")=="not-performed","Zero input acquired a fictitious normalized error or signal verdict");
}
void independentTimingAndFailures() {
    auto provider=carrier();provider.id="independent.chirp";provider.label_key="chirp";
    provider.prepare=[](const auto&,const Json& values){PreparedReferenceModel model;model.parameters=values;model.evaluate=[](const ReferenceModelPoint& point){const auto t=point.seconds();return std::polar(1.,static_cast<double>(2*std::numbers::pi_v<long double>*(35*t+200*t*t)));};return model;};
    register_reference_model(provider);const auto analysis=observations(true);const SignalSampleRange range{analysis->first_sample(),analysis->end_sample()-analysis->first_sample()};ReferenceFitOptions options;options.fit_dc_offset=true;options.minimum_delay_samples=-5;options.maximum_delay_samples=5;
    const auto matched=compare_reference_model(analysis,range,reference_model_provider(provider.id),Json::object(),options);
    require(matched.delay_samples==3 && matched.tied_delays.size()==1 && matched.residual_rms<3e-8,"An independent chirp did not align to its known integer delay");
    auto manual=options;manual.minimum_delay_samples=manual.maximum_delay_samples=0;manual.time_offset_seconds=3./4096;
    require(compare_reference_model(analysis,range,provider,Json::object(),manual).residual_rms<3e-8,"Manual time alignment changed the declared reference clock");
    auto fixed=provider;fixed.prepare=[](const auto&,const Json& values){PreparedReferenceModel model;model.parameters=values;model.evaluate=[](const auto&){return std::complex<double>{1,0};};return model;};
    rejected([&]{compare_reference_model(analysis,range,fixed,Json::object(),options);},"An unidentifiable constant/gain/DC fit was accepted");
    try {compare_reference_model(analysis,range,carrier(),{{"frequency_hz",5000}},options);throw std::runtime_error("Invalid whole model parameters accepted");}catch(const ReferenceModelError& error){require(error.resource_namespace=="independent-model" && error.issues.at(0).at("field")=="frequency_hz","Provider issues lost their resource or field");}
    auto small=options;small.maximum_model_evaluations=1;rejected([&]{compare_reference_model(analysis,range,provider,Json::object(),small);},"Unbounded model evaluation accepted");
    small=options;small.result_payload_bytes=100;rejected([&]{compare_reference_model(analysis,range,provider,Json::object(),small);},"Model output allocation budget ignored");
    small=options;small.workspace_payload_bytes=1;rejected([&]{compare_reference_model(analysis,range,provider,Json::object(),small);},"Model alignment workspace budget ignored");
    small=options;small.maximum_span_visits=1;rejected([&]{compare_reference_model(analysis,range,provider,Json::object(),small);},"Model span traversal budget ignored");
    small=options;small.maximum_duration_seconds=.001;rejected([&]{compare_reference_model(analysis,range,provider,Json::object(),small);},"Model time span budget ignored");
    std::stop_source cancel;cancel.request_stop();rejected([&]{compare_reference_model(analysis,range,provider,Json::object(),options,cancel.get_token());},"Canceled model comparison published output");
    std::stop_source during;unsigned evaluations=0;auto interrupted=provider;interrupted.prepare=[&](const auto&,const Json& values){PreparedReferenceModel model;model.parameters=values;model.evaluate=[&](const auto&){if(++evaluations==10)during.request_stop();return std::complex<double>{1,0};};return model;};
    rejected([&]{compare_reference_model(analysis,range,interrupted,Json::object(),options,during.get_token());},"A canceled running model published a partial result");require(evaluations>=10 && evaluations<=1024,"Model cancellation exceeded its bounded polling interval");
}
void pointProtocols() {
    constexpr std::uint32_t rate=1'000'001;const double manual=.25/rate;
    const auto truth=[](long double position){auto value=std::fmod(position,19.L);if(value<0)value+=19;return std::complex<double>(static_cast<double>(value*value/361),static_cast<double>((value+1)/20));};
    auto source=std::make_shared<SignalWindow>();source->stream={rate,433000000,SampleFormat::cf32_le};source->segments.push_back({UINT64_MAX-1000,{},{}});
    for(unsigned n=0;n<64;++n){const auto value=truth(n+3.25L);source->segments.back().samples.emplace_back(static_cast<float>(value.real()),static_cast<float>(value.imag()));}
    auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));ReferenceFitOptions fit;fit.fit_complex_gain=false;fit.time_offset_seconds=manual;fit.minimum_delay_samples=-5;fit.maximum_delay_samples=5;
    for(const auto version:{1u,2u,3u}) {
        ReferenceModelProvider provider;provider.id="independent.point-protocol-"+std::to_string(version);provider.resource_namespace="independent-model";provider.label_key="label";provider.protocol_version=version;provider.parameters=[](const auto&){return Json::array();};provider.describe=[](const auto&,const Json&){return Json::array();};
        provider.prepare=[=](const auto&,const Json& parameters){PreparedReferenceModel result;result.parameters=parameters;const auto evaluate=[=](const ReferenceModelPoint& point){
            if(version>=3)require(point.time_offset_seconds==manual,"v3 merged the integer delay into manual seconds");else require(point.integer_offset_samples==0,"An older provider received the new split-delay calling convention");
            const auto position=version>=3?point.offset_samples()+static_cast<long double>(point.time_offset_seconds)*rate:static_cast<long double>(point.sample_offset)+static_cast<long double>(point.time_offset_seconds)*rate;return truth(position);};result.workspace_payload_bytes=sizeof(evaluate);result.evaluate=evaluate;return result;};register_reference_model(provider);
        const auto result=compare_reference_model(analysis,{analysis->first_sample(),64},reference_model_provider(provider.id),Json::object(),fit);require(result.delay_samples==3 && result.tied_delays.size()==1 && result.residual_rms<4e-8 && result.method.at("provider_protocol_version")==version,"Point protocol compatibility lost the known integer delay or manual fraction");
    }
}
}
int main(){try{fitsAndClocks();independentTimingAndFailures();pointProtocols();std::cout<<"Independent reference providers, gain/DC fits, timing ambiguity, gaps, exact origins, rational clocks, split-delay compatibility and bounded failures passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
