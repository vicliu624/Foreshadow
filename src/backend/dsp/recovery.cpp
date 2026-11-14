#include "signalscope/backend/dsp/recovery.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_set>

namespace signalscope {
namespace {
void add(std::size_t& total,std::size_t value){if(value>SIZE_MAX-total)throw std::length_error("Recovery payload overflow");total+=value;}
template<class T>void capacity(std::size_t& total,const std::vector<T>& values){if(values.capacity()>SIZE_MAX/sizeof(T))throw std::length_error("Recovery capacity overflow");add(total,values.capacity()*sizeof(T));}
void text(std::size_t& total,const std::string& value){add(total,value.capacity());}
struct Metadata {
    std::size_t bytes=0,nodes=0,maximum=0,minimum_bytes=0;
    void token(std::size_t count){if(count>maximum-minimum_bytes)throw std::length_error("Recovery metadata string budget exceeded");minimum_bytes+=count;}
    void walk(const Json& value,unsigned depth=0){if(depth>16 || ++nodes>2048)throw std::length_error("Recovery metadata node/depth budget exceeded");if(value.is_object())for(const auto& [key,item]:value.items()){token(key.size());walk(item,depth+1);}else if(value.is_array())for(const auto& item:value)walk(item,depth+1);else if(value.is_string())token(value.get_ref<const std::string&>().size());else {token(1);if(value.is_number_float() && !std::isfinite(value.get<double>()))throw std::invalid_argument("Recovery metadata contains a non-finite number");}}
    void inspect(const Json& value){walk(value);const auto length=value.dump().size();if(length>maximum-bytes)throw std::length_error("Recovery metadata wire budget exceeded");bytes+=length;}
};
void range(const SignalSampleRange& value,const SignalSampleRange& parent){if(!value.count || value.first<parent.first || value.first>UINT64_MAX-value.count || value.first-parent.first>parent.count || value.count>parent.count-(value.first-parent.first))throw std::invalid_argument("Recovery support lies outside the selected clock");}
}
std::size_t RecoveryData::payload_bytes() const {
    std::size_t bytes=0;capacity(bytes,stages);capacity(bytes,sequences);capacity(bytes,frames);capacity(bytes,metrics);
    for(const auto& m:metrics){text(bytes,m.id);text(bytes,m.label_key);text(bytes,m.unit_key);text(bytes,m.reason_key);}
    for(const auto& stage:stages){text(bytes,stage.id);text(bytes,stage.label_key);capacity(bytes,stage.inputs);}
    for(const auto& sequence:sequences){text(bytes,sequence.id);text(bytes,sequence.label_key);text(bytes,sequence.domain_key);capacity(bytes,sequence.values);for(const auto& value:sequence.values){text(bytes,value.issue_key);capacity(bytes,value.inputs);}}
    for(const auto& frame:frames){text(bytes,frame.id);text(bytes,frame.label_key);text(bytes,frame.validation_scope_key);capacity(bytes,frame.checks);for(const auto& check:frame.checks){text(bytes,check.id);text(bytes,check.label_key);}}
    return bytes;
}
void validate_recovery(const RecoveryResult& result,const RecoveryBudget& budget) {
    if(!result.analysis || !result.analysis->sample_count() || result.provider_id.empty() || result.resource_namespace.empty() || !result.method_version)throw std::invalid_argument("Recovery lacks its source or provider identity");
    range(result.range,{result.analysis->first_sample(),result.analysis->end_sample()-result.analysis->first_sample()});
    if(result.range.count>budget.maximum_input_positions || result.payload_bytes()>budget.result_payload_bytes || result.workspace_payload_bytes>budget.workspace_payload_bytes || result.operations>budget.maximum_operations || result.frames.size()>budget.maximum_frames || result.stages.size()>64 || result.sequences.size()>16)throw std::length_error("Recovery exceeds its declared budgets");
    if(result.kind!=RecoveryKind::digital && result.kind!=RecoveryKind::measurement && result.kind!=RecoveryKind::unsupported)throw std::invalid_argument("Unsupported recovery result capability");
    if(result.protocol_version<1 || result.protocol_version>researchRecoveryProtocolVersion)throw std::invalid_argument("Unsupported recovery protocol");
    if(result.metrics.size()>64 || (result.protocol_version==1 && !result.metrics.empty()))throw std::invalid_argument("Unsupported recovery metric capability");
    std::set<std::string> metricIds;for(const auto& m:result.metrics)if(m.id.empty() || m.label_key.empty() || !metricIds.insert(m.id).second || !std::isfinite(m.value) || (!m.available && m.reason_key.empty()))throw std::invalid_argument("Invalid recovery metric");
    if(result.kind!=RecoveryKind::digital && (!result.frames.empty() || (result.protocol_version==1 && !result.sequences.empty())))throw std::invalid_argument("Nondigital recovery contains digital payload");
    if(!result.parameters.is_object() || !result.method.is_object() || !result.issues.is_array())throw std::invalid_argument("Invalid recovery metadata shape");
    Metadata metadata{0,0,budget.metadata_bytes};metadata.inspect(result.event);metadata.inspect(result.parameters);metadata.inspect(result.method);metadata.inspect(result.issues);
    std::set<std::string> stageIds,sequenceIds,frameIds;std::size_t values=0,links=0;
    for(std::size_t i=0;i<result.stages.size();++i){const auto& stage=result.stages[i];if(stage.id.empty() || stage.label_key.empty() || !stageIds.insert(stage.id).second || !stage.parameters.is_object() || !stage.features.is_object() || !stage.conclusion.is_object())throw std::invalid_argument("Invalid recovery stage descriptor");for(const auto input:stage.inputs)if(input>=i)throw std::invalid_argument("Recovery stage dependency is not acyclic");metadata.inspect(stage.parameters);metadata.inspect(stage.features);metadata.inspect(stage.conclusion);}
    for(std::size_t i=0;i<result.sequences.size();++i){const auto& sequence=result.sequences[i];if(sequence.id.empty() || sequence.label_key.empty() || !sequenceIds.insert(sequence.id).second || sequence.stage>=result.stages.size() || static_cast<unsigned>(sequence.kind)>static_cast<unsigned>(RecoveryValueKind::complex))throw std::invalid_argument("Invalid recovery sequence descriptor");
        if(sequence.kind==RecoveryValueKind::complex){if(result.protocol_version<2 || sequence.domain_key.empty() || result.kind==RecoveryKind::unsupported)throw std::invalid_argument("Complex sequence lacks its v2 measurement capability");}
        else if(result.kind!=RecoveryKind::digital || sequence.reference || !sequence.domain_key.empty())throw std::invalid_argument("Invalid sequence capability");
        if(sequence.values.size()>budget.maximum_values-values)throw std::length_error("Recovery value budget exceeded");
        values+=sequence.values.size();
        for(const auto& value:sequence.values){if(!std::isfinite(value.metric) || !std::isfinite(value.complex_value.real()) || !std::isfinite(value.complex_value.imag()) || (!value.known && value.issue_key.empty()) || (sequence.kind==RecoveryValueKind::bit && value.value>1) || (sequence.kind==RecoveryValueKind::byte && value.value>255))throw std::invalid_argument("Invalid or unexplained recovered value");
            if(sequence.reference && value.inputs.empty())throw std::invalid_argument("Generated reference cannot be an observation leaf");
            if(value.support.count)range(value.support,result.range);else if(value.inputs.empty())throw std::invalid_argument("Recovered value lacks source support");
            if(value.inputs.size()>budget.maximum_links-links)throw std::length_error("Recovery dependency budget exceeded");
            links+=value.inputs.size();
            for(const auto& input:value.inputs)if(input.sequence>=i || !input.count || input.first>result.sequences[input.sequence].values.size() || input.count>result.sequences[input.sequence].values.size()-input.first)throw std::invalid_argument("Invalid recovery value dependency");
        }
    }
    for(const auto& frame:result.frames){if(frame.id.empty() || !frameIds.insert(frame.id).second || frame.label_key.empty() || !frame.fields.is_object() || frame.sequence>=result.sequences.size() || result.sequences[frame.sequence].kind!=RecoveryValueKind::byte || !frame.count || frame.first>result.sequences[frame.sequence].values.size() || frame.count>result.sequences[frame.sequence].values.size()-frame.first || static_cast<unsigned>(frame.state)>static_cast<unsigned>(RecoveryFrameState::truncated))throw std::invalid_argument("Invalid recovered frame range or state");
        bool passed=false;std::set<std::string> checkIds;for(const auto& check:frame.checks){if(check.id.empty() || !checkIds.insert(check.id).second || check.label_key.empty() || check.stage>=result.stages.size() || static_cast<unsigned>(check.state)>static_cast<unsigned>(RecoveryCheckState::fail))throw std::invalid_argument("Invalid recovered check descriptor");metadata.inspect(check.evidence);if(check.required && check.state==RecoveryCheckState::pass)passed=true;
            if(frame.state==RecoveryFrameState::validated && (check.state==RecoveryCheckState::fail || (check.required && check.state!=RecoveryCheckState::pass)))throw std::invalid_argument("A validated frame has failed or incomplete checks");}
        if(frame.state==RecoveryFrameState::validated){if(!passed || frame.validation_scope_key.empty())throw std::invalid_argument("A validated frame lacks a declared validation scope");for(std::size_t i=frame.first;i<std::size_t(frame.first)+frame.count;++i)if(!result.sequences[frame.sequence].values[i].known)throw std::invalid_argument("A validated frame contains unknown bytes");}metadata.inspect(frame.fields);
    }
}
RecoveryResult recover_signal(std::shared_ptr<const WaveformAnalysis> analysis,SignalSampleRange selection,const RecoveryProvider& provider,const Json& parameters,const RecoveryBudget& budget,Json event,std::stop_token stop) {
    if(!supports_recovery(provider) || !analysis || !analysis->sample_count() || !parameters.is_object())throw std::invalid_argument("Invalid recovery request");
    range(selection,{analysis->first_sample(),analysis->end_sample()-analysis->first_sample()});
    if(selection.count>budget.maximum_input_positions)throw std::length_error("Recovery input budget exceeded");
    Metadata metadata{0,0,budget.metadata_bytes};metadata.inspect(parameters);metadata.inspect(event);if(stop.stop_requested())throw std::runtime_error("Recovery canceled");
    RecoveryContext context{analysis,selection,budget,&event};auto data=provider.recover(context,parameters,stop);if(stop.stop_requested())throw std::runtime_error("Recovery canceled");
    if(!data.issues.is_array())throw std::invalid_argument("Invalid recovery issues");
    for(const auto& issue:data.issues)if(!issue.is_object() || !issue.contains("label_key") || !issue.at("label_key").is_string() || issue.at("label_key").get_ref<const std::string&>().empty())throw std::invalid_argument("Recovery issue lacks a resource key");
    metadata.inspect(data.issues);if(!data.issues.empty())throw RecoveryParameterError(provider.resource_namespace,std::move(data.issues));
    RecoveryResult result;static_cast<RecoveryData&>(result)=std::move(data);result.analysis=std::move(analysis);result.range=selection;result.provider_id=provider.id;result.resource_namespace=provider.resource_namespace;result.method_version=provider.method_version;result.protocol_version=provider.protocol_version;result.event=std::move(event);validate_recovery(result,budget);return result;
}
std::vector<SignalSampleRange> recovery_input_ranges(const RecoveryResult& result,std::uint32_t sequence,std::uint32_t first,std::uint32_t count,std::size_t maximumVisits,std::size_t maximumRanges) {
    std::vector<RecoveryLink> pending{{sequence,first,count}};std::map<std::uint64_t,std::uint64_t> support;std::size_t visits=0,steps=0;std::unordered_set<std::uint64_t> visited;
    const auto stepLimit=maximumVisits>SIZE_MAX/16?SIZE_MAX:maximumVisits*16;const auto step=[&]{if(++steps>stepLimit)throw std::length_error("Recovery dependency lookup budget exceeded");};
    while(!pending.empty()){const auto current=pending.back();pending.pop_back();if(current.sequence>=result.sequences.size() || !current.count || current.first>result.sequences[current.sequence].values.size() || current.count>result.sequences[current.sequence].values.size()-current.first)throw std::invalid_argument("Invalid recovery selection");
        for(std::size_t i=current.first;i<std::size_t(current.first)+current.count;++i){step();const auto key=(std::uint64_t(current.sequence)<<32)|i;if(visited.contains(key))continue;if(++visits>maximumVisits)throw std::length_error("Recovery source traversal budget exceeded");visited.insert(key);const auto& value=result.sequences[current.sequence].values[i];if(value.inputs.empty()){
                if(!value.support.count || value.support.first>UINT64_MAX-value.support.count)throw std::invalid_argument("Invalid recovery leaf support");
                auto begin=value.support.first,end=begin+value.support.count;auto next=support.lower_bound(begin);
                if(next!=support.begin()){const auto previous=std::prev(next);if(previous->second>=begin){begin=previous->first;end=std::max(end,previous->second);next=support.erase(previous);}}
                while(next!=support.end() && next->first<=end){end=std::max(end,next->second);next=support.erase(next);}support.emplace(begin,end);if(support.size()>maximumRanges)throw std::length_error("Recovery source range budget exceeded");
            }else for(const auto& input:value.inputs){if(input.sequence>=current.sequence || !input.count || input.first>result.sequences[input.sequence].values.size() || input.count>result.sequences[input.sequence].values.size()-input.first)throw std::invalid_argument("Invalid recovery dependency");bool remaining=false;for(std::size_t at=input.first;at<std::size_t(input.first)+input.count;++at){step();if(!visited.contains((std::uint64_t(input.sequence)<<32)|at)){remaining=true;break;}}if(!remaining)continue;if(pending.size()>=maximumVisits-visits)throw std::length_error("Recovery dependency traversal exceeded its budget");pending.push_back(input);}}
    }
    std::vector<SignalSampleRange> ranges;ranges.reserve(support.size());for(const auto& [begin,end]:support)ranges.push_back({begin,end-begin});return ranges;
}
}
