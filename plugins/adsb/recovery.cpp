#include "core/demod.hpp"
#include "core/input_policy.hpp"
#include "signalscope/backend/dsp/recovery.hpp"
#include <algorithm>
#include <cmath>
namespace signalscope::adsb {
namespace {
RecoveryData recover(const RecoveryContext& context,const Json& parameters,std::stop_token stop) {
    RecoveryData data;data.kind=RecoveryKind::digital;
    if(!parameters.empty()){data.issues.push_back({{"label_key","issue.parameters"}});return data;}
    const auto& analysis=*context.analysis;const auto rate=analysis.sample_rate();
    if(!supports_rate(rate)){data.issues.push_back({{"label_key","outside"}});return data;}
    const auto& i=analysis.trace("i");const auto& q=analysis.trace("q");
    validate_analysis_trace(analysis,i);validate_analysis_trace(analysis,q);
    if(analysis_trace_spans(analysis,i)!=analysis.spans || analysis_trace_spans(analysis,q)!=analysis.spans)throw std::invalid_argument("ADS-B recovery requires observed I/Q mappings");
    const auto frameSamples=(std::uint64_t(rate)*120+999999)/1000000;
    // Conservative bound includes scanning, slot integration and CRC work.
    const auto cost=std::uint64_t(5*(4*frameSamples+4096));
    if(context.range.count>context.budget.maximum_operations/cost)throw std::length_error("ADS-B recovery operation budget exceeded");
    data.workspace_payload_bytes=(frameSamples+1)*sizeof(float)+4096*sizeof(std::complex<float>);
    if(data.workspace_payload_bytes>context.budget.workspace_payload_bytes)throw std::length_error("ADS-B recovery workspace budget exceeded");
    data.stages={{"ppm-crc","stage.ppm",{},Json::object(),Json::object(),{{"label_key","scope"}}}};
    data.sequences={{"bytes","raw",0,RecoveryValueKind::byte,{},"",false}};
    Demodulator demod(rate);std::vector<std::complex<float>> chunk;chunk.reserve(4096);
    const auto sink=[&](const Frame& frame){
        if(data.frames.size()>=std::min<std::size_t>(32,context.budget.maximum_frames) || data.sequences[0].values.size()+14>context.budget.maximum_values)throw std::length_error("ADS-B recovery frame/value budget exceeded");
        auto& values=data.sequences[0].values;const auto first=static_cast<std::uint32_t>(values.size());
        for(unsigned byte=0;byte<14;++byte){RecoveryValue value;value.value=frame.bytes[byte];value.known=true;
            const auto begin=std::uint64_t(std::floor((8.+8*byte)*rate/1000000.));
            const auto end=std::uint64_t(std::ceil((16.+8*byte)*rate/1000000.));
            value.support={frame.first_sample+begin,end-begin};values.push_back(std::move(value));}
        RecoveryFrame result;result.id=std::to_string(frame.first_sample);result.label_key="event";result.validation_scope_key="scope";
        result.sequence=0;result.first=first;result.count=14;result.state=RecoveryFrameState::validated;
        result.checks={{"crc24","check.crc",0,RecoveryCheckState::pass,true,{{"remainder",0}}}};
        result.fields={{"message",{{"label_key","raw"},{"value",frame.fields.hex}}},{"type",{{"label_key","type"},{"value",frame.fields.type_code}}}};
        data.frames.push_back(std::move(result));
        if(data.payload_bytes()>context.budget.result_payload_bytes)throw std::length_error("ADS-B recovery result budget exceeded");
    };
    for(const auto& span:analysis.spans){const auto begin=std::max(span.first_sample,context.range.first),end=std::min(span.first_sample+span.count,context.range.first+context.range.count);if(begin>=end)continue;
        for(auto at=begin;at<end;){if(stop.stop_requested())throw std::runtime_error("ADS-B recovery canceled");
            const auto count=std::min<std::uint64_t>(4096,end-at);chunk.clear();
            for(std::uint64_t n=0;n<count;++n){const auto index=span.first_value+at+n-span.first_sample;chunk.emplace_back(float(i.values[index]),float(q.values[index]));}
            demod.feed(chunk,at,at==begin && span.discontinuous_before,sink);at+=count;
        }
    }
    data.operations=context.range.count*cost;
    data.method={{"id","adsb-ppm-crc24"},{"version",1},{"bit_correction",false},{"validation","crc-only"}};
    return data;
}
}
RecoveryProvider recovery_provider(){return {"adsb-ppm-crc24","adsb","label","parsing",1,1,
    [](const RecoveryContext&,const Json&){return Json::array();},recover};}
}
