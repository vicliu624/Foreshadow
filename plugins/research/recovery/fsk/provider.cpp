#include "signalscope/backend/dsp/recovery.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace signalscope::recovery {
namespace {
Json number(const char* key,double value,double low,double high){return {{"key",key},{"type","number"},{"extension",true},{"label_key",key},{"default",value},{"minimum",low},{"maximum",high}};}
Json integer(const char* key,unsigned value,unsigned low,unsigned high){auto field=number(key,value,low,high);field["type"]="integer";field["default"]=value;return field;}
void issue(RecoveryData& data,const char* field,const char* message){data.issues.push_back({{"field",field},{"label_key",message}});}
Json describe(const RecoveryContext& context,const Json&) {
    if(!context.analysis)throw std::invalid_argument("Recovery descriptors require an input clock");
    const auto rate=context.analysis->sample_rate();auto fields=Json::array({integer("samples_per_symbol",16,4,65536),integer("symbol_offset_samples",0,0,static_cast<unsigned>(std::min<std::uint64_t>(65535,context.range.count?context.range.count-1:65535))),
        number("zero_hz",-double(rate)/8.,-double(rate)/2,rate/2.),number("one_hz",rate/8.,-double(rate)/2,rate/2.),number("minimum_rms",.001,0,10),number("minimum_separation_db",6,0,80),integer("payload_bytes",4,1,1024)});
    fields.push_back({{"key","bit_order"},{"type","enum"},{"extension",true},{"label_key","bit_order"},{"default","msb-first"},{"choices",Json::array({"msb-first","lsb-first"})},{"choice_labels",{{"msb-first",{{"label_key","bit_order.msb"}}},{"lsb-first",{{"label_key","bit_order.lsb"}}}}}});return fields;
}
struct Projection {std::complex<double> zero{},one{},m0{1,0},m1{1,0},step0{},step1{};long double power=0;};
bool project(const WaveformAnalysis& analysis,const std::vector<double>& i,const std::vector<double>& q,SignalSampleRange support,Projection& state,std::uint64_t& operations,std::stop_token stop) {
    auto span=std::upper_bound(analysis.spans.begin(),analysis.spans.end(),support.first,[](auto value,const auto& item){return value<item.first_sample;});if(span!=analysis.spans.begin())--span;
    for(std::uint64_t n=0;n<support.count;++n){if((n&1023)==0 && stop.stop_requested())throw std::runtime_error("Recovery canceled");const auto sample=support.first+n;
        while(span!=analysis.spans.end() && sample>=span->first_sample+span->count)++span;
        if(span==analysis.spans.end() || sample<span->first_sample || (n && sample==span->first_sample && span->discontinuous_before))return false;
        const auto index=span->first_value+static_cast<std::size_t>(sample-span->first_sample);const std::complex<double> value{i[index],q[index]};
        state.zero+=value*state.m0;state.one+=value*state.m1;state.power+=std::norm(value);state.m0*=state.step0;state.m1*=state.step1;operations+=2;
    }return true;
}
std::uint16_t checksum(const std::vector<RecoveryValue>& bytes,std::size_t first,std::size_t count,std::uint64_t& operations) {
    std::uint16_t state=0xffff;for(std::size_t i=first;i<first+count;++i){state^=static_cast<std::uint16_t>(bytes[i].value<<8);for(unsigned bit=0;bit<8;++bit){state=static_cast<std::uint16_t>((state<<1)^((state&0x8000)?0x1021:0));++operations;}}return state;
}
RecoveryData recover(const RecoveryContext& context,const Json& supplied,std::stop_token stop) {
    RecoveryData data;data.kind=RecoveryKind::digital;const auto fields=describe(context,supplied);
    for(const auto& field:fields){const auto key=field.at("key").get<std::string>();const auto value=supplied.value(key,field.at("default"));data.parameters[key]=value;const auto type=field.at("type").get<std::string>();
        const bool valid=type=="enum"?std::find(field.at("choices").begin(),field.at("choices").end(),value)!=field.at("choices").end():value.is_number() && (type!="integer" || value.is_number_integer()) && std::isfinite(value.get<double>()) && value>=field.at("minimum") && value<=field.at("maximum");if(!valid)issue(data,key.c_str(),"issue.parameter");}
    for(const auto& [key,value]:supplied.items()){(void)value;if(!data.parameters.contains(key))issue(data,key.c_str(),"issue.unknown");}if(!data.issues.empty())return data;
    const auto n=data.parameters.at("samples_per_symbol").get<unsigned>(),offset=data.parameters.at("symbol_offset_samples").get<unsigned>(),payload=data.parameters.at("payload_bytes").get<unsigned>();const double f0=data.parameters.at("zero_hz"),f1=data.parameters.at("one_hz"),minimum=data.parameters.at("minimum_rms"),separation=data.parameters.at("minimum_separation_db");const bool msb=data.parameters.at("bit_order")=="msb-first";
    if(f0==f1 || std::abs(f0)>=context.analysis->sample_rate()/2. || std::abs(f1)>=context.analysis->sample_rate()/2.){issue(data,"one_hz","issue.tones");return data;}
    const auto frameSize=payload+2;const auto available=context.range.count-offset,symbols=(available+n-1)/n,bytes=(symbols+7)/8,frames=(bytes+frameSize-1)/frameSize;
    if(symbols>UINT32_MAX || symbols*2+bytes>context.budget.maximum_values || symbols+bytes>context.budget.maximum_links || frames>std::min<std::size_t>(64,context.budget.maximum_frames) || available*4+symbols*8+bytes*16>context.budget.maximum_operations)throw std::length_error("FSK recovery exceeds its value/operation budget");
    const auto estimate=(symbols*2+bytes)*(sizeof(RecoveryValue)+sizeof(RecoveryLink)+64)+frames*(sizeof(RecoveryFrame)+3*sizeof(RecoveryCheck)+512)+4096;
    if(estimate>context.budget.result_payload_bytes || sizeof(Projection)>context.budget.workspace_payload_bytes)throw std::length_error("FSK recovery exceeds its payload/workspace budget");
    const auto& inphase=context.analysis->trace("i"),&quadrature=context.analysis->trace("q");validate_analysis_trace(*context.analysis,inphase);validate_analysis_trace(*context.analysis,quadrature);if(analysis_trace_spans(*context.analysis,inphase)!=context.analysis->spans || analysis_trace_spans(*context.analysis,quadrature)!=context.analysis->spans)throw std::invalid_argument("Recovery requires full observed I/Q trace coverage");
    data.workspace_payload_bytes=sizeof(Projection);data.stages={
        {"tone-decision","stage.tones",{},data.parameters,Json::object(),{{"label_key","conclusion.manual"}}},
        {"symbol-to-bit","stage.bits",{0},{{"mapping","zero-tone-to-0; one-tone-to-1"}},Json::object(),Json::object()},
        {"bit-packing","stage.bytes",{1},{{"bit_order",msb?"msb-first":"lsb-first"}},Json::object(),Json::object()},
        {"fixed-frame","stage.frames",{2},{{"payload_bytes",payload},{"crc_polynomial",0x1021},{"crc_initial",0xffff},{"crc_xorout",0},{"crc_reflected",false},{"crc_storage","big-endian"}},Json::object(),{{"label_key","frame.scope"}}}};
    data.sequences={{"symbols","sequence.symbols",0,RecoveryValueKind::symbol,{}},{"bits","sequence.bits",1,RecoveryValueKind::bit,{}},{"bytes","sequence.bytes",2,RecoveryValueKind::byte,{}}};
    auto& symbolValues=data.sequences[0].values;auto& bitValues=data.sequences[1].values;auto& byteValues=data.sequences[2].values;symbolValues.reserve(static_cast<std::size_t>(symbols));bitValues.reserve(static_cast<std::size_t>(symbols));byteValues.reserve(static_cast<std::size_t>(bytes));data.frames.reserve(static_cast<std::size_t>(frames));
    std::uint64_t erased=0;
    for(std::uint32_t s=0;s<symbols;++s){if(stop.stop_requested())throw std::runtime_error("Recovery canceled");RecoveryValue value;value.support={context.range.first+offset+std::uint64_t(s)*n,std::min<std::uint64_t>(n,available-std::uint64_t(s)*n)};Projection projection;
        projection.step0=std::polar(1.,-2*std::numbers::pi*f0/context.analysis->sample_rate());projection.step1=std::polar(1.,-2*std::numbers::pi*f1/context.analysis->sample_rate());
        if(value.support.count<n)value.issue_key="issue.partial_symbol";
        else if(!project(*context.analysis,inphase.values,quadrature.values,value.support,projection,data.operations,stop))value.issue_key="issue.missing";
        else {const auto power=static_cast<double>(projection.power/n),zero=std::norm(projection.zero)/(double(n)*n),one=std::norm(projection.one)/(double(n)*n);const auto floor=std::max(power*1e-12,1e-300);value.metric=10*std::log10((std::max(zero,one)+floor)/(std::min(zero,one)+floor));value.value=one>zero?1:0;
            if(power<minimum*minimum || power==0)value.issue_key="issue.low_power";else if(value.metric<=separation)value.issue_key="issue.ambiguous";else value.known=true;}
        if(!value.known)++erased;
        symbolValues.push_back(std::move(value));RecoveryValue bit;bit.value=symbolValues.back().value;bit.known=symbolValues.back().known;bit.issue_key=bit.known?"":"issue.bit_erasure";bit.inputs.push_back({0,s,1});bitValues.push_back(std::move(bit));
    }
    for(std::uint32_t b=0;b<bytes;++b){RecoveryValue value;const auto first=b*8,count=static_cast<std::uint32_t>(std::min<std::uint64_t>(8,symbols-first));value.known=count==8;for(unsigned bit=0;bit<count;++bit){value.value|=bitValues[first+bit].value<<(msb?7-bit:bit);value.known=value.known && bitValues[first+bit].known;++data.operations;}value.issue_key=value.known?"":count<8?"issue.partial_byte":"issue.byte_erasure";value.inputs.push_back({1,first,count});byteValues.push_back(std::move(value));}
    for(std::uint32_t f=0;f<frames;++f){if(stop.stop_requested())throw std::runtime_error("Recovery canceled");const auto first=f*frameSize,count=static_cast<std::uint32_t>(std::min<std::uint64_t>(frameSize,bytes-first));const bool complete=count==frameSize && (first+count<bytes || (symbols%8==0 && available%n==0));bool known=true;for(std::size_t i=first;i<std::size_t(first)+count;++i)known=known && byteValues[i].known;
        auto check=RecoveryCheckState::unknown;Json evidence={{"complete",complete},{"all_bytes_known",known}};if(complete && known){const auto expected=checksum(byteValues,first,payload,data.operations),received=static_cast<std::uint16_t>((byteValues[first+payload].value<<8)|byteValues[first+payload+1].value);check=expected==received?RecoveryCheckState::pass:RecoveryCheckState::fail;evidence.update({{"expected",expected},{"received",received}});}
        RecoveryFrame frame;frame.id="fixed-"+std::to_string(f);frame.label_key="frame.label";frame.validation_scope_key="frame.scope";frame.sequence=2;frame.first=first;frame.count=count;frame.state=!complete?RecoveryFrameState::truncated:check==RecoveryCheckState::pass?RecoveryFrameState::validated:check==RecoveryCheckState::fail?RecoveryFrameState::invalid:RecoveryFrameState::candidate;
        frame.checks={{"known-bytes","check.bytes",3,known?RecoveryCheckState::pass:RecoveryCheckState::unknown,true,{{"known",known}}},{"crc16","check.crc",3,check,true,evidence},{"fec","check.fec",3,RecoveryCheckState::not_applicable,false,Json::object()}};frame.fields={{"payload_bytes",{{"label_key","payload_bytes"},{"value",payload}}},{"business_semantics",{{"label_key","field.business_unknown"}}}};data.frames.push_back(std::move(frame));
    }
    data.stages[0].features={{"symbols",symbols},{"erasures",erased},{"metric","stronger-tone/other-tone-projection-power-dB; not calibrated confidence"}};data.stages[2].features={{"bytes",bytes},{"trailing_partial_bits",symbols%8}};
    data.method={{"id","manual-noncoherent-2fsk-fixed-crc16"},{"version",1},{"input_sample_rate_hz",context.analysis->sample_rate()},{"symbol_rate_hz",double(context.analysis->sample_rate())/n},{"uninterpreted_prefix_positions",offset},{"trailing_partial_positions",available%n},{"synchronization","manual-symbol-grid; no acquisition"},{"classification","not-performed"},{"validation","checksum-only-under-explicit-fixed-frame-and-clock-assumptions"},{"operation_unit","sample-projection-or-bit-update"},{"gap_policy","erase-symbol; no fill or phase bridge"}};
    if(data.payload_bytes()>context.budget.result_payload_bytes || data.operations>context.budget.maximum_operations)throw std::length_error("Completed FSK recovery exceeds its budget");
    return data;
}
}
RecoveryProvider fsk(){return {"manual-2fsk-fixed-crc16","recovery-fsk","label","about",1,1,describe,recover};}
}
