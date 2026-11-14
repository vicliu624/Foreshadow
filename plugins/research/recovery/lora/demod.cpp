#include "demod.hpp"
#include <kiss_fft.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace signalscope::recovery::lora {
namespace {
constexpr unsigned maximumCandidates=32,maximumTimingWindows=9,maximumHypotheses=32768;
struct Work {
    std::uint64_t used=0,limit;std::stop_token stop;
    void add(std::uint64_t count){if(stop.stop_requested())throw std::runtime_error("LoRa demodulation canceled");if(count>limit-used)throw std::length_error("LoRa demodulation work limit exceeded");used+=count;}
};
unsigned steps(double low,double high,double step) {
    if(!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(step) || low>high || step<=0)throw std::invalid_argument("Invalid LoRa hypothesis grid");
    const auto intervals=(high-low)/step;if(intervals>maximumHypotheses || std::abs(intervals-std::round(intervals))>1e-7)throw std::invalid_argument("LoRa grid endpoints must be on its finite step grid");return static_cast<unsigned>(std::llround(intervals))+1;
}
double modulo(double value,unsigned modulus){auto result=std::fmod(value,modulus);return result<0?result+modulus:result;}
unsigned symbol(double value,unsigned chips){return static_cast<unsigned>(std::floor(modulo(value,chips)+.5))%chips;}
struct Fft {
    std::vector<kiss_fft_cpx> input,output;
    std::vector<std::max_align_t> plan_memory;
    std::vector<std::complex<float>> reference;
    std::vector<double> folded;
    kiss_fft_cfg plan=nullptr;
    static std::size_t planBytes(unsigned size){std::size_t bytes=0;(void)kiss_fft_alloc(static_cast<int>(size),0,nullptr,&bytes);return bytes;}
    static std::size_t bytes(unsigned n,unsigned fft,unsigned bins){return std::size_t(fft)*2*sizeof(kiss_fft_cpx)+((planBytes(fft)+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t))*sizeof(std::max_align_t)+std::size_t(n)*sizeof(std::complex<float>)+std::size_t(bins)*sizeof(double);}
    Fft(unsigned n,unsigned fft,unsigned bins,double bandwidth,unsigned rate):input(fft),output(fft),plan_memory((planBytes(fft)+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t)),reference(n),folded(bins) {
        auto available=plan_memory.size()*sizeof(std::max_align_t);plan=kiss_fft_alloc(static_cast<int>(fft),0,plan_memory.data(),&available);if(!plan)throw std::bad_alloc();
        for(unsigned i=0;i<n;++i){const auto t=double(i)/rate,cycles=bandwidth*t/2-bandwidth*t*t/(2*(double(n)/rate));reference[i]=std::polar(1.f,static_cast<float>(2*std::numbers::pi*cycles));}
    }
    std::size_t payload() const{return (input.capacity()+output.capacity())*sizeof(kiss_fft_cpx)+plan_memory.capacity()*sizeof(std::max_align_t)+reference.capacity()*sizeof(std::complex<float>)+folded.capacity()*sizeof(double);}
    Peak measure(const WaveformAnalysis& analysis,SignalSampleRange support,const DemodOptions& options,std::uint64_t origin,unsigned n,unsigned chips,Work& work) {
        Peak peak;peak.support=support;if(support.count<n){peak.issue_key="issue.partial_symbol";return peak;}
        const auto& is=analysis.trace("i").values;const auto& qs=analysis.trace("q").values;
        auto span=std::upper_bound(analysis.spans.begin(),analysis.spans.end(),support.first,[](auto at,const auto& item){return at<item.first_sample;});if(span!=analysis.spans.begin())--span;
        long double power=0;std::fill(input.begin(),input.end(),kiss_fft_cpx{});
        for(unsigned i=0;i<n;++i){work.add(1);const auto at=support.first+i;while(span!=analysis.spans.end() && at>=span->first_sample+span->count)++span;if(span==analysis.spans.end() || at<span->first_sample || (i && at==span->first_sample && span->discontinuous_before)){peak.issue_key="issue.missing";return peak;}
            const auto index=span->first_value+static_cast<std::size_t>(at-span->first_sample);std::complex<double> value{is[index],qs[index]};power+=std::norm(value);value*=std::polar(1.,-2*std::numbers::pi*options.carrier_offset_hz*double(at-origin)/analysis.sample_rate());if(options.inverted_iq)value=std::conj(value);value*=std::complex<double>(reference[i]);input[i]={static_cast<float>(value.real()),static_cast<float>(value.imag())};
        }
        peak.rms=std::sqrt(static_cast<double>(power/n));if(peak.rms<options.minimum_rms || peak.rms==0){peak.issue_key="issue.low_power";return peak;}
        // Charge a radix-2 butterfly upper bound and each folded power bin;
        // these workload units are recorded, not a claim of CPU FLOP counts.
        work.add(std::uint64_t(input.size())*(std::bit_width(input.size())-1)/2+input.size());kiss_fft(plan,input.data(),output.data());
        std::fill(folded.begin(),folded.end(),0);long double total=0;for(std::size_t i=0;i<output.size();++i){const auto power=double(output[i].r)*output[i].r+double(output[i].i)*output[i].i;folded[i%folded.size()]+=power;total+=power;}
        const auto best=static_cast<std::size_t>(std::max_element(folded.begin(),folded.end())-folded.begin());const auto a=folded[(best+folded.size()-1)%folded.size()],b=folded[best],c=folded[(best+1)%folded.size()];const auto denominator=a-2*b+c;const auto fraction=denominator<0?std::clamp(.5*(a-c)/denominator,-.5,.5):0.;peak.bin=modulo((double(best)+fraction)/options.fft_padding,chips);peak.concentration=total>0?std::clamp(double(b*options.fft_padding/total),0.,1.):0;
        peak.known=peak.concentration>=options.minimum_concentration;peak.issue_key=peak.known?"":"issue.diffuse_peak";return peak;
    }
};
bool sameBytes(const Hypothesis& a,const Hypothesis& b){if(a.byte_count!=b.byte_count)return false;for(unsigned i=0;i<a.byte_count;++i)if(a.bytes[i]!=b.bytes[i] || a.known[i]!=b.known[i])return false;return true;}
bool closer(const Hypothesis& a,const Hypothesis& b){return std::array<double,3>{std::abs(double(a.shift)),std::abs(a.drift),std::abs(a.bias)}<std::array<double,3>{std::abs(double(b.shift)),std::abs(b.drift),std::abs(b.bias)};}
bool better(const Hypothesis& a,const Hypothesis& b){return a.domain_score!=b.domain_score?a.domain_score<b.domain_score:a.score<b.score;}
bool equalRank(const Hypothesis& a,const Hypothesis& b){return a.domain_score==b.domain_score && a.score==b.score;}
void retain(Demodulation& result,Hypothesis candidate) {
    if(!result.candidates.empty()){const auto& best=*std::min_element(result.candidates.begin(),result.candidates.end(),better);if(better(candidate,best))result.best_candidates_complete=true;if(candidate.domain_score<best.domain_score)result.domain_best_complete=true;}
    for(auto& previous:result.candidates)if(sameBytes(previous,candidate)){if(better(candidate,previous)){candidate.matches=previous.matches+1;previous=candidate;}else {++previous.matches;if(equalRank(candidate,previous) && closer(candidate,previous)){candidate.matches=previous.matches;previous=candidate;}}return;}
    candidate.matches=1;if(result.candidates.size()<maximumCandidates)result.candidates.push_back(candidate);
    else {result.candidate_limit_reached=true;const auto worst=std::max_element(result.candidates.begin(),result.candidates.end(),better),best=std::min_element(result.candidates.begin(),result.candidates.end(),better);if(candidate.domain_score==best->domain_score && worst->domain_score==best->domain_score)result.domain_best_complete=false;if(equalRank(candidate,*best) && !better(candidate,*worst))result.best_candidates_complete=false;if(better(candidate,*worst))*worst=candidate;}
}
Json candidateJson(const Hypothesis& candidate){std::string hex,known;constexpr char digits[]="0123456789ABCDEF";for(unsigned i=0;i<candidate.byte_count;++i){hex+=digits[candidate.bytes[i]>>4];hex+=digits[candidate.bytes[i]&15];known+=candidate.known[i]?'1':'0';}return {{"hex_candidate",hex},{"known_bytes",known},{"score",candidate.score},{"timing_shift_samples",candidate.shift},{"drift_bins_per_symbol",candidate.drift},{"bias_bins",candidate.bias},{"equivalent_hypotheses",candidate.matches}};}
}
std::vector<RecoveryValue> hypothesis_symbols(const Demodulation& result,const Hypothesis& hypothesis) {
    std::vector<RecoveryValue> values;values.reserve(hypothesis.symbol_count);const auto& options=result.options;const auto reference=options.use_reference?result.peaks[hypothesis.peak_first].bin:0.;const auto referenceKnown=!options.use_reference || result.peaks[hypothesis.peak_first].known;
    const auto distance=options.use_reference?double(options.data_offset_samples-options.reference_offset_samples)/result.symbol_samples:0.;const auto first=hypothesis.peak_first+(options.use_reference?1:0);
    for(unsigned i=0;i<hypothesis.symbol_count;++i){const auto& peak=result.peaks[first+i];RecoveryValue value;value.support=peak.support;value.value=symbol(peak.bin-reference-hypothesis.drift*(i+distance)+hypothesis.bias,result.chips);value.known=peak.known && referenceKnown;value.issue_key=!referenceKnown?"issue.reference_unknown":peak.issue_key;value.metric=peak.concentration;values.push_back(std::move(value));}return values;
}
Demodulation demodulate(const RecoveryContext& context,const DemodOptions& options,std::stop_token stop,const CandidatePolicy* policy) {
    if(!context.analysis || options.packet.sf<7 || options.packet.sf>12 || !options.bandwidth_hz || options.maximum_symbols<8 || options.maximum_symbols>1024 || !options.timing_step_samples || options.timing_radius_samples>INT32_MAX || options.timing_radius_samples%options.timing_step_samples || (options.fft_padding!=1 && options.fft_padding!=2 && options.fft_padding!=4 && options.fft_padding!=8) || !std::isfinite(options.minimum_rms) || options.minimum_rms<0 || !std::isfinite(options.minimum_concentration) || options.minimum_concentration<0 || options.minimum_concentration>1 || !std::isfinite(options.carrier_offset_hz) || std::abs(options.carrier_offset_hz)>=context.analysis->sample_rate()/2.)throw std::invalid_argument("Invalid complete LoRa demodulation options");
    if(!context.range.count || context.range.first>UINT64_MAX-context.range.count || context.range.first<context.analysis->first_sample() || context.range.first>=context.analysis->end_sample() || context.range.count>context.analysis->end_sample()-context.range.first)throw std::invalid_argument("Invalid LoRa selected clock range");
    if(context.range.count>context.budget.maximum_input_positions)throw std::length_error("LoRa selection exceeds its input limit");
    if(std::abs(options.drift_min)>1 || std::abs(options.drift_max)>1 || std::abs(options.bias_min)>(1u<<options.packet.sf) || std::abs(options.bias_max)>(1u<<options.packet.sf))throw std::invalid_argument("LoRa drift or bias exceeds its numerical domain");
    const auto rate=context.analysis->sample_rate(),chips=1u<<options.packet.sf;if(rate%options.bandwidth_hz || !std::has_single_bit(rate/options.bandwidth_hz) || rate/options.bandwidth_hz>16)throw std::invalid_argument("LoRa input requires a power-of-two integer samples/chip ratio in 1..16");
    const auto n=chips*(rate/options.bandwidth_hz),fft=n*options.fft_padding,bins=chips*options.fft_padding;
    if(fft>262144 || options.timing_step_samples>n || options.timing_radius_samples>n/4 || options.data_offset_samples>=context.range.count || (options.use_reference && (options.reference_offset_samples>=options.data_offset_samples || options.reference_offset_samples>=context.range.count)))throw std::invalid_argument("LoRa symbol/reference extent exceeds its selected input");
    const auto timingCount=1+2*options.timing_radius_samples/options.timing_step_samples,driftCount=steps(options.drift_min,options.drift_max,options.drift_step),biasCount=steps(options.bias_min,options.bias_max,options.bias_step);
    if(timingCount>maximumTimingWindows || std::uint64_t(timingCount)*driftCount*biasCount>maximumHypotheses)throw std::length_error("LoRa hypothesis count exceeds its limit");
    const auto rawCount=std::size_t(timingCount)*(options.maximum_symbols+(options.use_reference?1:0));const auto fixed=sizeof(Demodulation)+rawCount*(sizeof(Peak)+64)+maximumCandidates*sizeof(Hypothesis)+2*options.maximum_symbols*(sizeof(RecoveryValue)+64)+65536+(policy?policy->workspace_payload_bytes:0);
    if(fixed>context.budget.workspace_payload_bytes || Fft::bytes(n,fft,bins)>context.budget.workspace_payload_bytes-fixed)throw std::length_error("LoRa FFT and search workspace exceed their budget");
    const auto& i=context.analysis->trace("i");const auto& q=context.analysis->trace("q");validate_analysis_trace(*context.analysis,i);validate_analysis_trace(*context.analysis,q);if(analysis_trace_spans(*context.analysis,i)!=context.analysis->spans || analysis_trace_spans(*context.analysis,q)!=context.analysis->spans)throw std::invalid_argument("LoRa recovery requires full observed I/Q traces");
    Demodulation result;result.options=options;result.domain_ranked=policy && bool(policy->evaluate);result.chips=chips;result.symbol_samples=n;result.fft_size=fft;result.peaks.reserve(rawCount);result.candidates.reserve(maximumCandidates);Work work{0,context.budget.maximum_operations,stop};
    Fft transform(n,fft,bins,options.bandwidth_hz,rate);result.workspace_payload_bytes=fixed+transform.payload();if(result.workspace_payload_bytes>context.budget.workspace_payload_bytes)throw std::length_error("Allocated LoRa workspace exceeds its budget");
    bool fallback=false;for(int shift=-static_cast<int>(options.timing_radius_samples);shift<=static_cast<int>(options.timing_radius_samples);shift+=static_cast<int>(options.timing_step_samples)) {
        const auto start=static_cast<std::int64_t>(options.data_offset_samples)+shift,reference=static_cast<std::int64_t>(options.reference_offset_samples)+shift;
        if(start<0 || std::uint64_t(start)>=context.range.count || (options.use_reference && (reference<0 || std::uint64_t(reference)+n>context.range.count)))continue;
        const auto count=static_cast<unsigned>(std::min<std::uint64_t>(options.maximum_symbols,(context.range.count-start+n-1)/n)),peakFirst=static_cast<unsigned>(result.peaks.size());
        if(options.use_reference)result.peaks.push_back(transform.measure(*context.analysis,{context.range.first+static_cast<unsigned>(reference),n},options,context.range.first,n,chips,work));
        for(unsigned s=0;s<count;++s){const auto at=static_cast<std::uint64_t>(start)+std::uint64_t(s)*n;result.peaks.push_back(transform.measure(*context.analysis,{context.range.first+at,std::min<std::uint64_t>(n,context.range.count-at)},options,context.range.first,n,chips,work));}
        if(!fallback || std::abs(shift)<std::abs(result.chosen_shift)){fallback=true;result.chosen_peak_first=peakFirst;result.chosen_symbols=count;result.chosen_shift=shift;result.chosen_bias=std::clamp(0.,options.bias_min,options.bias_max);result.chosen_drift=std::clamp(0.,options.drift_min,options.drift_max);}
        for(unsigned d=0;d<driftCount;++d)for(unsigned b=0;b<biasCount;++b){work.add(384+count);++result.attempts;Hypothesis candidate;candidate.peak_first=peakFirst;candidate.symbol_count=count;candidate.shift=shift;candidate.drift=options.drift_min+d*options.drift_step;candidate.bias=options.bias_min+b*options.bias_step;const auto values=hypothesis_symbols(result,candidate);
            if(options.packet.explicit_header){if(values.size()<8)continue;if(std::all_of(values.begin(),values.begin()+8,[](const auto& value){return value.known;})){std::array<std::uint16_t,8> head;for(unsigned h=0;h<8;++h)head[h]=static_cast<std::uint16_t>(values[h].value);const auto header=inspect_header(head,options.packet.sf,options.packet.correct_single_bit);if(!header.parameters_known || header.checksum!=RecoveryCheckState::pass)continue;}}
            auto budget=context.budget;budget.maximum_operations-=work.used;auto packetOptions=options.packet;packetOptions.symbol_samples=n;const auto packet=decode_symbols(values,packetOptions,budget,stop);work.add(packet.operations);if(!packet.header_usable || packet.bytes.empty())continue;
            unsigned unknown=0,headerDistance=0,payloadDistance=0,erasures=0;for(unsigned k=0;k<packet.words.size();++k){const auto& word=packet.words[k];if(options.packet.explicit_header && k<5)headerDistance+=word.distance;else payloadDistance+=word.distance;erasures+=word.width-std::popcount(static_cast<unsigned>(word.known_mask));}
            candidate.byte_count=static_cast<unsigned>(packet.bytes.size());for(unsigned k=0;k<candidate.byte_count;++k){candidate.bytes[k]=packet.bytes[k].value;candidate.known[k]=packet.bytes[k].known;unknown+=!packet.bytes[k].known;}
            candidate.score={packet.applied.payload_crc?(packet.payload_checksum==RecoveryCheckState::pass?0u:packet.payload_checksum==RecoveryCheckState::unknown?1u:2u):0u,packet.truncated?1u:0u,unknown,payloadDistance,headerDistance,erasures};if(policy && policy->evaluate){work.add(policy->work_per_packet);const auto priority=policy->evaluate(packet,stop);if(!priority.retain)continue;candidate.domain_score=priority.score;}retain(result,candidate);
        }
    }
    if(!fallback)throw std::invalid_argument("No LoRa timing hypothesis lies in the selection");
    std::stable_sort(result.candidates.begin(),result.candidates.end(),[](const auto& a,const auto& b){return !equalRank(a,b)?better(a,b):closer(a,b);});
    if(!result.candidates.empty()){const auto& best=result.candidates.front();result.chosen_peak_first=best.peak_first;result.chosen_symbols=best.symbol_count;result.chosen_shift=best.shift;result.chosen_bias=best.bias;result.chosen_drift=best.drift;}
    result.operations=work.used;return result;
}
RecoveryData demodulation_evidence(const RecoveryContext& context,Demodulation result,std::stop_token stop,const Hypothesis* representative) {
    Hypothesis chosen;chosen.peak_first=result.chosen_peak_first;chosen.symbol_count=result.chosen_symbols;chosen.shift=result.chosen_shift;chosen.bias=result.chosen_bias;chosen.drift=result.chosen_drift;if(representative)chosen=*representative;
    auto budget=context.budget;if(result.operations>budget.maximum_operations)throw std::length_error("LoRa search exhausted its work budget");budget.maximum_operations-=result.operations;auto packetOptions=result.options.packet;packetOptions.symbol_samples=result.symbol_samples;auto data=packet_evidence(hypothesis_symbols(result,chosen),packetOptions,budget,stop);data.operations+=result.operations;
    const auto existing=data.payload_bytes(),rawBytes=result.peaks.size()*(sizeof(RecoveryValue)+48)+4096;
    if(existing>context.budget.result_payload_bytes || rawBytes>context.budget.result_payload_bytes-existing)throw std::length_error("LoRa observation evidence exceeds its result budget");
    unsigned bestCount=0;if(!result.candidates.empty())for(const auto& candidate:result.candidates)bestCount+=equalRank(candidate,result.candidates.front());
    const bool ambiguous=bestCount>1 || !result.best_candidates_complete;
    for(auto& stage:data.stages)for(auto& input:stage.inputs)input+=2;
    for(auto& sequence:data.sequences){sequence.stage+=2;for(auto& value:sequence.values)for(auto& input:value.inputs)input.sequence+=2;}
    for(auto& frame:data.frames){frame.sequence+=2;for(auto& check:frame.checks)check.stage+=2;}
    data.stages[0].id="lora-aligned-symbols";data.stages[0].inputs={0,1};
    data.stages.insert(data.stages.begin(),{{"lora-fft-observations","stage.peaks",{},{{"fft_size",result.fft_size},{"padding",result.options.fft_padding},{"folded_bins",result.chips*result.options.fft_padding},{"iq_inverted",result.options.inverted_iq},{"carrier_offset_hz",result.options.carrier_offset_hz}},{{"metric","fractional folded FFT bin; not probability"}},Json::object()},
        {"lora-hypothesis-selection","stage.hypothesis",{0},{{"drift_min",result.options.drift_min},{"drift_max",result.options.drift_max},{"drift_step",result.options.drift_step},{"bias_min",result.options.bias_min},{"bias_max",result.options.bias_max},{"bias_step",result.options.bias_step}},{{"hypotheses_evaluated",result.attempts},{"retained_distinct_candidates",result.candidates.size()},{"best_distinct_candidates",bestCount},{"candidate_limit_reached",result.candidate_limit_reached},{"best_candidates_complete",result.best_candidates_complete}},{{"label_key",ambiguous?"conclusion.ambiguous":result.candidates.empty()?"conclusion.no_header":"conclusion.hypothesis"}}}});
    RecoverySequence raw{"fft-peaks","sequence.peaks",0,RecoveryValueKind::symbol,{}};raw.values.reserve(result.peaks.size());for(const auto& peak:result.peaks){RecoveryValue value;value.value=symbol(peak.bin,result.chips);value.known=peak.known;value.metric=peak.bin;value.support=peak.support;value.issue_key=peak.issue_key;raw.values.push_back(std::move(value));}
    RecoveryValue calibration;calibration.known=true;calibration.inputs={{0,0,static_cast<unsigned>(result.peaks.size())}};RecoverySequence selection{"selected-hypothesis","sequence.hypothesis",1,RecoveryValueKind::symbol,{std::move(calibration)}};
    const auto first=chosen.peak_first+(result.options.use_reference?1:0);for(unsigned i=0;i<data.sequences[0].values.size();++i)data.sequences[0].values[i].inputs={{0,first+i,1},{1,0,1}};
    data.sequences.insert(data.sequences.begin(),std::move(selection));data.sequences.insert(data.sequences.begin(),std::move(raw));
    if(!representative && ambiguous && !result.candidates.empty()){auto& bytes=data.sequences[8].values;for(unsigned i=0;i<bytes.size();++i){bool uncertain=!result.best_candidates_complete;for(unsigned c=1;c<bestCount;++c)uncertain=uncertain || i>=result.candidates[c].byte_count || !result.candidates[c].known[i] || result.candidates[c].bytes[i]!=bytes[i].value;if(uncertain){bytes[i].known=false;bytes[i].issue_key="issue.ambiguous_payload";}}for(auto& frame:data.frames){if(frame.state==RecoveryFrameState::validated)frame.state=RecoveryFrameState::candidate;for(auto& check:frame.checks){check.evidence["conditional_on_representative_hypothesis"]=true;if(check.id=="known-bytes")check.state=RecoveryCheckState::unknown;}}}
    if(representative){for(auto& frame:data.frames){if(frame.state==RecoveryFrameState::validated)frame.state=RecoveryFrameState::candidate;for(auto& check:frame.checks)check.evidence["conditional_on_application_representative"]=true;}data.method["application_representative"]=true;}Json alternatives=Json::array();for(const auto& candidate:result.candidates){auto json=candidateJson(candidate);if(result.domain_ranked)json["domain_score"]=candidate.domain_score;alternatives.push_back(std::move(json));}data.method.update({{"id","bounded-lora-fft-recovery"},{"symbol_samples",result.symbol_samples},{"input_sample_rate_hz",context.analysis->sample_rate()},{"bandwidth_hz",result.options.bandwidth_hz},{"fft_padding",result.options.fft_padding},{"selected_timing_shift_samples",chosen.shift},{"selected_drift_bins_per_symbol",chosen.drift},{"selected_bias_bins",chosen.bias},{"use_reference",result.options.use_reference},{"data_offset_samples",result.options.data_offset_samples},{"reference_offset_samples",result.options.reference_offset_samples},{"hypotheses_evaluated",result.attempts},{"search_grid_complete",true},{"best_candidates_complete",result.best_candidates_complete},{"candidate_limit_reached",result.candidate_limit_reached},{"payload_ambiguous",ambiguous},{"candidates",std::move(alternatives)},{"provenance","includes all FFT observations used to select the hypothesis"},{"operation_unit","charged radix-2 butterfly bound, folded bin, sample mix, symbol mapping or codeword test"}});data.workspace_payload_bytes=std::max(data.workspace_payload_bytes,result.workspace_payload_bytes);
    if(data.payload_bytes()>context.budget.result_payload_bytes)throw std::length_error("Allocated LoRa observation evidence exceeds its budget");
    return data;
}
}
